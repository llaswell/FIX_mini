#ifndef FIX_INTERFACE_H_
#define FIX_INTERFACE_H_

#include <iostream>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unordered_map>
#include <map>
#include <sys/time.h>
#include <string>
#include <time.h>
#include <pthread.h>
#include <cstdio>
#include <string>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>


#include "boost/bind.hpp"
#include "boost/thread/thread.hpp"
#include "boost/thread/mutex.hpp"
#include "boost/shared_ptr.hpp"


#include "Order.h"
#include "AsyncLogger.h"
#include "TradeState.h"
#include "TradeStateEnvelope.h"
#include "split.h"
#include "util.h"
#include "SnapshotLogfile.h"

enum FIX_STATUS { FS_SUSPENDED, FS_NORMAL , FS_LOGON, FS_MASSCANCEL, FS_RESEND, FS_RETRAN, FS_DISCONNECTED };

typedef std::map<std::string,TradeStateEnvelope> OSM;
typedef std::map<std::string,TradeStateEnvelope>::iterator OSMI;

typedef std::vector<Order> OV;
typedef std::vector<Order>::iterator OVI;

class FIX_MessageEnvelope {
public:
	FIX_MessageEnvelope();
	std::string message;
	std::string messageType;
	size_t	secs;
	long nanos;
	bool logged;
};


class FIX_Interface {
public:
	FIX_Interface(std::string _hostIP, const unsigned short _hostPort);

	~FIX_Interface();

	bool socketSendMessage(char* msg);
	bool initConnection();
	void closeSocket(int &sock);
	void readSocketThread();

	void handleRawMessage(char *_message);
	void finishMessage(char* _message,std::string _messageType,char _possDup,uint32_t _seqNo=0,std::string _sendingTimeString="");
	void createOrderCancelReplaceBody(const Order& _order,
		Order& _newOrder,char* _body);

	void createHeartbeatBody(char* _body,std::string _testString);
	void createLogonBody(char* _body,int _heartBeatInterval=15,
		bool _resetSequence=false);

	void createNewOrderBody(const Order& _order,char* _body,std::string _resendClause="");
	void createOrderCancelBody(const Order& _order,char* _body);
	void createOrderCancelBody(std::string _orderID,char* _body);
	void createMassCancelBody(char* _body);
	void createResendRequest(char* _body,uint32_t _begin,uint32_t _end);
	void createReject(char* _body,uint32_t _rejectedSeqNo, std::string _messageType, uint16_t _reason,const std::string& _text);
	void createSequenceReset(char* _body,bool _gapFill,uint32_t _newSequenceNum);
	void massCancel();

	bool sendMessage(char* _message, std::string _messageType,char _possDup,uint32_t _seqNo=0,std::string _sendingTimeString="");
	void printMessage(char* _message);

	bool logon();
	int calcChecksum(char* _message);
	int calcChecksum(char* _message,long bufLen);
	uint32_t getNextOrderID(void);
	std::string getUTCTimeString(void);
	void parseMessage(char* _message,std::unordered_map<std::string, std::string>& _parsedMessageMap,std::string& _messageType,uint32_t& _messageSeqNo);
	void logWriter();

	void resendMessages(uint32_t _beginMessage, uint32_t _endMessage );
	void requestResend(long _beginMessage, long _endMessage);
	std::string getStatusString(FIX_STATUS _status);
	void setStatus(FIX_STATUS _status );
	void sendGapFill(uint32_t _begin,uint32_t _end);
	void requestResend(uint32_t _begin,uint32_t _end);
	void sendReject(uint32_t _rejectedSeqNo, std::string _messageType, uint16_t _reason,const std::string& _text);
	void rejectMessage(uint16_t _reason,std::string _text,std::unordered_map<std::string,std::string>& _parsedMessageMap);
	bool getMessageTag(std::string _tag,std::string& _value,unordered_map<std::string, std::string>& _parsedMessageMap);
	bool sendHeartbeat(std::string _testString); 
	void heartbeatThread();
	bool goodSeqNo(uint32_t _messageSeqNo, std::string _messageType, bool _overRide=false );
	void handleSequenceReset(std::unordered_map<std::string,std::string>& _parsedMessageMap,std::string _messageType,uint32_t _messageSeqNo);
	void handleParsedMessage(std::unordered_map<std::string,std::string>& _parsedMessageMap,std::string _messageType,uint32_t _messageSeqNo);
	bool findOrder(uint32_t _orderID,OSMI& _mapI,OVI& _vectorI);
	bool resetSeqNo(uint32_t _newSeqNo,uint32_t _messageSeqNo,std::string _messageType);
	void handleExecution(OSMI _context,long _lastQty,double _price,std::string _exchange, bool _takeLiquidity);
	void persist();
	void readTradeState();
	void listOrders();
	void getNextSeqNo(uint32_t& _seqNo);
	bool removeStrandedOrder(OSMI _context,timespec& _time);
	bool locateOrderByTime(vector<Order>& _orderVectorRef,timespec& _time,OVI& p);
private:

	int bodyLength;
	char sep;
	uint32_t nextIncomingSeqNum;
	uint32_t lastReceivedSeqNum;
	uint32_t nextOutgoingSeqNum;
	uint32_t nextCL_ID;	
	char possDup;
	std::string dest;
	int persist_fd;
	char* persistPtr;
	size_t persist_size;


	std::vector<FIX_MessageEnvelope> outgoingMessages;

	AsyncLogger* fixLogPtr;
	AsyncLogger* fixEngineLogPtr;

	string hostIP;
	unsigned short hostPort;

	int sock;
	bool connected;
	pthread_mutex_t socketMutex;

	bool writeLogs;

	FIX_STATUS status;


	struct timespec lastReceiveTime;
	struct timespec lastSendTime;

	uint32_t retranRequestCount;
	uint32_t maxRetranRequests;	
	uint32_t messageHighWaterMark;

	volatile bool keepAlive;
	
	boost::shared_ptr<boost::thread> m_heartbeatThread;
	boost::shared_ptr<boost::thread> m_readThread;

	OSM tradeStateMap;

public:
	std::string account;
	std::string senderCompID;
	std::string senderSubID;
	std::string targetCompID;
	uint32_t heartBeatInterval;

	std::map<std::string,double> takeMap;
	std::map<std::string,double> provideMap;

	double secFeeRate;
	double executionFeeRate;
	

	bool sim;

	SnapshotLogfile* orderLogPtr;

	bool cancelOrder(uint32_t _orderID);
	bool cancelReplaceOrder(uint32_t _orderID,double _price,long _shares);

	void disconnect();
	void startThreads();

	void getTradeState(OSMI _context,TradeState& _tradeStateRef,vector<Order>& _orderVectorRef);
	OSMI getTradeContext(std::string _symbol);

	bool sendOrder(Order& _order,OSMI _context);
	bool cancelOrder(Order& _order,OSMI _context);
	bool cancelReplaceOrder(Order& _order,double _newPrice, long _newSize,OSMI _context);
	inline bool findTradeContext(std::string _symbol,OSMI& _mapI);
	bool locateOrderInVector(vector<Order>& _orderVector,uint32_t _ordID,OVI& _foundIterator);
	bool removeOrderFromVector(vector<Order>& _orderVector,uint32_t _ordID);
	void newOrderStatsUpdate(OSMI _context, Order& _orderRef );
	void setOvernightPostion(OSMI _context, long _overnightPosition);
	bool readEasyToBorrow(const std::string& _easy);
	bool readRates(const string& _rates);
	void clearRejects();
};

#endif // FIX_INTERFACE_H_
