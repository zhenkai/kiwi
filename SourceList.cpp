#include "SourceList.h"
#include "Params.h"

static struct pollfd pfds[1];
static pthread_mutex_t mutex;
static pthread_mutexattr_t ccn_attr;

static SourceList *gSourceList;
static enum ccn_upcall_res handle_source_info_content(struct ccn_closure *selfp,
												   enum ccn_upcall_kind kind,
												   struct ccn_upcall_info *info);
SourceList::SourceList() {
	gSourceList = this;
	readNdnParams();
	nh = new NdnHandler();
	pfds[0].fd = ccn_get_connection_fd(nh->h);
	pfds[0].events = POLLIN;

	alivenessTimer = new QTimer(this);
	connect(alivenessTimer, SIGNAL(timeout()), this, SLOT(checkAliveness()));
	alivenessTimer->start(ALIVE_PERIOD / 60 * 1000);
	pthread_mutexattr_init(&ccn_attr);
	pthread_mutexattr_settype(&ccn_attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mutex, &ccn_attr);
	acceptStaleData = true;
	discoverClosure = (struct ccn_closure *)calloc(1, sizeof(struct ccn_closure));
	discoverClosure->p = &handle_source_info_content;
	enumerate();
	bRunning = true;
	start();
}

SourceList::~SourceList() {
	bRunning = false;
	if (isRunning())
		wait();
	if (nh != NULL) {
		delete nh;
		nh = NULL;
	}
	if (discoverClosure != NULL)
		free(discoverClosure);
	
	QHash<QString, MediaSource *>::const_iterator it = list.constBegin();
	for(; it != list.constEnd(); it++) {
		MediaSource *ms = NULL;
		ms = it.value();
		if (ms != NULL)
			delete ms;
	}
}

void SourceList::readNdnParams() {
	QDomDocument settings;
	QString configFileName = QDir::homePath() + "/.actd/.config";
	QFile config(configFileName);
	if (!config.exists()) {
		fprintf(stderr, "Config file does not exist\n");
		abort();
	}
	if (!config.open(QIODevice::ReadOnly)) {
		fprintf(stderr, "Can not open config file\n");
		abort();
	}
	if (!settings.setContent(&config)) {
		config.close();
		fprintf(stderr, "Can not parse config file\n");
		abort();
	}

	QDomElement docElem = settings.documentElement();
	QDomNode n = docElem.firstChild();
	while (!n.isNull()) {
		if (n.nodeName() == "confName") {
			confName = n.toElement().text();
		}
		n = n.nextSibling();
	}

	if (confName == "") {
		fprintf(stderr, "Null conference name\n");
		abort();
	}
}

void SourceList::checkAliveness() {

    QHash<QString, MediaSource *>::iterator it = list.begin();
    while (it != list.end())
    {
		MediaSource *ms = it.value();
		if (ms && ms->getNeedRemove()) {
			QString msUser = ms->getUsername();
			it = list.erase(it);
			if (!ms->getEmitted())
				emit mediaSourceLeft(msUser);
			delete ms;
		}
		else {
			++it;
		}
    }

}

void SourceList::imageDecoded(QString name, IplImage *image) {
	emit mediaSourceImageDecoded(name, image);
}


void SourceList::enumerate() {
    ccn_charbuf *path = NULL;
    path = ccn_charbuf_create();
	// e.g. /ndn/broadcast/conference/asfd/video-list
	ccn_name_from_uri(path, BROADCAST_PREFIX);
    ccn_name_append_str(path, confName.toLocal8Bit().constData());
	ccn_name_append_str(path, "video-list");

    QHash<QString, MediaSource *>::const_iterator it = list.constBegin();
	QList<QString> toExclude;
    while (it != list.constEnd())
    {
		MediaSource *ms = it.value();
		if (ms && ms->getNeedExclude()) {
			toExclude.append(ms->getUsername());
		}
		++it;
    }

	//QString username = getenv("KIWI_USERNAME");
	//toExclude.append(username);

	expressEnumInterest(path, toExclude);
}

void SourceList::stopAcceptingStaleData() {
	acceptStaleData = false;
}

void SourceList::expressEnumInterest(struct ccn_charbuf *interest, QList<QString> &toExclude) {

	if (toExclude.size() == 0) {
		struct ccn_charbuf *templ = NULL;

		if(acceptStaleData) {
			templ = ccn_charbuf_create();
            ccn_charbuf_append_tt(templ, CCN_DTAG_Interest, CCN_DTAG); // <interest>
            ccn_charbuf_append_tt(templ, CCN_DTAG_Name, CCN_DTAG); // <name>
            ccn_charbuf_append_closer(templ); // </name>
			// accept stale data
			ccnb_tagged_putf(templ, CCN_DTAG_AnswerOriginKind, "%d", 4); 
			ccn_charbuf_append_closer(templ); //</interest>
		}
		int res = ccn_express_interest(nh->h, interest, discoverClosure, templ);
		if (res < 0) {
			fprintf(stderr, "express interest failed!");
		}
		ccn_charbuf_destroy(&interest);
		if (templ != NULL)
			ccn_charbuf_destroy(&templ);
		return;
	}

	struct ccn_charbuf **excl = NULL;
	if (toExclude.size() > 0) {
		excl = new ccn_charbuf *[toExclude.size()];
		for (int i = 0; i < toExclude.size(); i ++) {
			QString compName = toExclude.at(i);
			struct ccn_charbuf *comp = ccn_charbuf_create();
			ccn_name_init(comp);
			ccn_name_append_str(comp, compName.toStdString().c_str());
			excl[i] = comp;
			comp = NULL;
		}
		qsort(excl, toExclude.size(), sizeof(excl[0]), NdnHandler::nameCompare);
	}

	int begin = 0;
	bool excludeLow = false;
	bool excludeHigh = true;
	while (begin < toExclude.size()) {
		if (begin != 0) {
			excludeLow = true;
		}
		struct ccn_charbuf *templ = ccn_charbuf_create();
		ccn_charbuf_append_tt(templ, CCN_DTAG_Interest, CCN_DTAG); // <Interest>
		ccn_charbuf_append_tt(templ, CCN_DTAG_Name, CCN_DTAG); // <Name>
		ccn_charbuf_append_closer(templ); // </Name> 
		ccn_charbuf_append_tt(templ, CCN_DTAG_Exclude, CCN_DTAG); // <Exclude>
		if (excludeLow) {
			NdnHandler::excludeAll(templ);
		}
		for (; begin < toExclude.size(); begin++) {
			struct ccn_charbuf *comp = excl[begin];
			if (comp->length < 4) abort();
			// we are being conservative here
			if (interest->length + templ->length + comp->length > 1350) {
				break;
			}
			ccn_charbuf_append(templ, comp->buf + 1, comp->length - 2);
		}
		if (begin == toExclude.size()) {
			excludeHigh = false;
		}
		if (excludeHigh) {
			NdnHandler::excludeAll(templ);
		}
		ccn_charbuf_append_closer(templ); // </Exclude>
		if (acceptStaleData) {
			// accept stale data
			ccnb_tagged_putf(templ, CCN_DTAG_AnswerOriginKind, "%d", 4); 
		}

		ccn_charbuf_append_closer(templ); // </Interest> 
		int res = ccn_express_interest(nh->h, interest, discoverClosure, templ);
		if (res < 0) {
			fprintf(stderr, "express interest failed!");
		}
		ccn_charbuf_destroy(&templ);
	}
	ccn_charbuf_destroy(&interest);
	for (int i = 0; i < toExclude.size(); i++) {
		ccn_charbuf_destroy(&excl[i]);
	}
	if (excl != NULL) {
		delete []excl;
	}
}

void SourceList::handleSourceInfoContent(struct ccn_upcall_info *info) {
	size_t len = 0;
	const unsigned char *value = NULL;
	ccn_content_get_value(info->content_ccnb, info->pco->offset[CCN_PCO_E], info->pco, &value, &len);
	if (parseSourceInfoContent(value, len) < 0) {
		fprintf(stderr, "Invalid source info received.\n");
	}
	
	enumerate();
}

int SourceList::parseSourceInfoContent(const unsigned char *value, size_t len) {

    QByteArray buffer((const char *)(value));
    QDomDocument doc;
    if (!doc.setContent(buffer)) {
        return -1;
    }
   
    QString username, qPrefix;
	bool leaving = false;
    QDomElement docElem = doc.documentElement();  // <user> 
    QDomNode node = docElem.firstChild();  // <username>
    while (!node.isNull()) {
        if (node.nodeName() == "username") {
			username = node.toElement().text();
		} else
        if (node.nodeName() == "prefix") {
			qPrefix = node.toElement().text();
		} else 
		if (node.nodeName() == "leave") {
			leaving = true;
		}
        node = node.nextSibling();
    }

	if (username == "")
		return -1;
	
	if (!leaving && qPrefix == "")
		return -1;
	
	if (leaving)  {
		fprintf(stderr, "leaving message from %s received\n", username.toStdString().c_str());
		removeMediaSource(username);
	}
	else
		addMediaSource(username, qPrefix);

    return 0;
}

int SourceList::addMediaSource(QString username, QString prefix) {


	QHash<QString, MediaSource *>::const_iterator it = list.find(username);
	if (it != list.constEnd()) { // username exists
		MediaSource *ms = it.value();
		if (ms != NULL) {
			ms->refreshReceived();
			if (prefix != ms->getPrefix()) { // user updated prefix
				ms->setPrefix(prefix);
			}
			return 0;
		}
		else {
			list.remove(username);
		}
	}
	
	MediaSource *ms = new MediaSource(this, prefix, username);
	list.insert(username, ms);

	// do not report self to other module
	QSettings settings("UCLA_IRL", "KIWI");
	QString localUsername = settings.value("KiwiLocalUsername", QString("")).toString();
	if (username == localUsername) {
		fprintf(stderr, "ignore self\n");
		return 0;
	}

    emit mediaSourceAdded(username); 
    return 0;
}

// only removes from GUI
// keep it here until leave object expires
int SourceList::removeMediaSource(QString username) {
	MediaSource *ms = list[username];
	if (ms == NULL)
		return -1;
	
	//list.remove(username);

	emit mediaSourceLeft(username);

	ms->setEmitted();	
	ms->setEmitTime();
	//delete ms;
}

void SourceList::run() {
	int res = 0;
	res = ccn_run(nh->h, 0);
	while(res >= 0 && bRunning) {
		res = ccn_run(nh->h, 0);
		int ret = poll(pfds, 1, 10);
		if (ret >= 0) {
			pthread_mutex_lock(&mutex);
			res = ccn_run(nh->h, 0);
			pthread_mutex_unlock(&mutex);
		}
	}
}

MediaSource::MediaSource(QObject *parent, QString prefix, QString username) {
	decoder = new VideoStreamDecoder(BUF_SIZE);
	largestSeenSeq = 0;
	seq = 0;
	streaming = false;
	consecutiveTimeouts = 0;
	namePrefix = prefix;
	this->username = username;
	emitted = false;

	connect(this, SIGNAL(imageDecoded(QString, IplImage *)), parent, SLOT(imageDecoded(QString, IplImage *)));
	fetchClosure = (struct ccn_closure *)calloc(1, sizeof(struct ccn_closure));
	fetchClosure->data = this;
	fetchClosure->p = NULL;
	fetchPipelineClosure = (struct ccn_closure *)calloc(1, sizeof(struct ccn_closure));
	fetchPipelineClosure->data = this;
	fetchPipelineClosure->p = NULL;
	lastRefreshTime = QDateTime::currentDateTime();
}

MediaSource::~MediaSource() {
	if (fetchClosure != NULL)
		free(fetchClosure);
	if (decoder != NULL)
		delete decoder;
}

void MediaSource::refreshReceived() {
	lastRefreshTime = QDateTime::currentDateTime();
	fprintf(stderr, "Exclude for %s\n", username.toStdString().c_str());
}

bool MediaSource::getNeedExclude() {
	// always exclude those already leaved but had not been cleaned up yet
	if (emitted)
		return true;

	QDateTime now = QDateTime::currentDateTime();
	if (lastRefreshTime.secsTo(now) >= FRESHNESS) {
		return false;
	}

	return true;
}

bool MediaSource::getNeedRemove() {
	QDateTime now = QDateTime::currentDateTime();
	if (lastRefreshTime.secsTo(now) >= ALIVE_PERIOD) {
		return true;
	}

	if (emitted && emitTime.secsTo(now) >= FRESHNESS * 3) {
		return true;
	}

	return false;
}

void MediaSource::setEmitTime() {
	emitTime = QDateTime::currentDateTime();
}

bool MediaSource::needSendInterest() {
	return (largestSeenSeq + PENDING_INTEREST_NUM > seq);
}

void MediaSource::decodeImage(const unsigned char *buf, int len) {
	IplImage *decodedImage = decoder->decodeVideoFrame(buf, len);
	emit imageDecoded(username, decodedImage);
	free((void *)buf);
}

static enum ccn_upcall_res handle_source_info_content(struct ccn_closure *selfp,
												   enum ccn_upcall_kind kind,
												   struct ccn_upcall_info *info) {
	switch (kind) {
	case CCN_UPCALL_INTEREST_TIMED_OUT:
		if (gSourceList != NULL)  {
			// stop accepting stale data after first timeout
			gSourceList->stopAcceptingStaleData();
			gSourceList->enumerate();
		}

		break;

	case CCN_UPCALL_CONTENT_UNVERIFIED:
		break;
	case CCN_UPCALL_CONTENT:
		gSourceList->handleSourceInfoContent(info);
		break;
	default:
		break;
	}
	return CCN_UPCALL_RESULT_OK;

}


