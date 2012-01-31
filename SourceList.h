#ifndef SOURCELIST_H
#define SOURCELIST_H
#include "NdnHandler.h"
#include <QThread>
#include <QTimer>
#include <QHash>
#include <QDir>
#include <QFile>
#include <QtXml>
#include <pthread.h>
#include <poll.h>
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include "VideoStreamDecoder.h"

class MediaSource;
class MediaFetcher;
class SourceList: public QThread {
	Q_OBJECT
public:
	SourceList();
	virtual ~SourceList();

	void run();
	friend class MediaFetcher;
	void stopAcceptingStaleData(); 
	void handleSourceInfoContent(struct ccn_upcall_info *);
	void enumerate();

private:
	int parseSourceInfoContent(const unsigned char *value, size_t len);
	int addMediaSource(QString username, QString prefix);
	int removeMediaSource(QString username);
	void readNdnParams();
	void expressEnumInterest(struct ccn_charbuf *interest, QList<QString> &toExclude);

signals:
	void mediaSourceLeft(QString);
	void mediaSourceAdded(QString);
	void mediaSourceImageDecoded(QString, IplImage *);
public slots:
	void imageDecoded(QString, IplImage *);
	void checkAliveness();
private:
	QTimer *alivenessTimer;
	QString username;
	NdnHandler *nh;
	QString confName;
	QHash<QString, MediaSource *> list;
	struct ccn_closure *discoverClosure;
	bool acceptStaleData;
	bool bRunning;
};

struct buffer_t{
	unsigned char *buf;
	int size;
	int targetSize;
};

class MediaSource: public QObject {
	Q_OBJECT
public:
	MediaSource(QObject *parent, QString prefix, QString username);
	~MediaSource();
	void refreshReceived();
	bool isStreaming() { return streaming; }
	bool needSendInterest(); 
	bool getNeedExclude();
	bool getNeedRemove();
	QString getUsername() {return username; }
	QString getPrefix() {return namePrefix;}
	void setPrefix(QString prefix) {namePrefix = prefix; }
	void setStreaming(bool streaming) {this->streaming = streaming;}
	void incSeq() {seq ++;}
	void setSeq(long seq) {this->seq = seq;}
	long getSeq() {return seq;}
	void incTimeouts(){consecutiveTimeouts++;}
	void resetTimeouts();
	int getTimeouts() {return consecutiveTimeouts;}
	void decodeImage(const unsigned char *buf, int len);
	void setEmitted() {emitted = true;}
	bool getEmitted() {return emitted;}
	void setEmitTime();
	friend class MediaFetcher;
signals:
	void imageDecoded(QString, IplImage *);
private:
	VideoStreamDecoder *decoder;
	QString namePrefix;
	QString username;
	long seq;
	QDateTime lastRefreshTime;
	QDateTime emitTime;
	struct ccn_closure *fetchClosure;
	struct ccn_closure *fetchPipelineClosure;
	bool streaming;
	int consecutiveTimeouts;
	QHash<long, struct buffer_t *> frameBuffers;
	long largestSeenSeq;
	bool emitted;

};


#endif
