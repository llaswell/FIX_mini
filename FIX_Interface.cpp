#include "FIX_Interface.h"

std::string ordStatus2String(char _status) {
	switch( _status ) {
		case '0':
			return "NEW";
			break;
		case '1':
			return "PARTIALLY_FILLED";
			break;
		case '2':
			return "FILLED";
			break;
		case '3':
			return "DONE_FOR_DAY";
			break;
		case '4':
			return "CANCELED";
			break;
		case '5':
			return "REPLACED";
			break;
		case '6':
			return "PENDING_CANCEL";
			break;
		case '7':
			return "STOPPED";
			break;
		case '8':
			return "REJECTED";
			break;
		case '9':
			return "SUSPENDED";
			break;
		case 'A':
			return "PENDING_NEW";
			break;
		case 'B':
			return "Calculated";
			break;
		case 'C':
			return "Expired";
			break;
		case 'D':
			return "ACCEPTED_FOR_BIDDING";
			break;
		case 'E':
			return "PENDING_REPLACE";
			break;
		default:
			break;
	}
	return "UNKNOWN_CODE";
}

FIX_MessageEnvelope::FIX_MessageEnvelope() : secs(0),nanos(0),logged(false) {}

FIX_Interface::FIX_Interface(std::string _hostIP,unsigned short _hostPort) : hostIP(_hostIP),hostPort(_hostPort),nextCL_ID(1), sim(false), connected(false),status(FS_DISCONNECTED), writeLogs(true),sep('\1'),retranRequestCount(0),maxRetranRequests(5),keepAlive(true),persist_fd(-1),persistPtr(NULL) {
	pthread_mutex_init(&socketMutex,NULL);
	fixLogPtr = new AsyncLogger("fixLog.txt",true);
	fixEngineLogPtr = new AsyncLogger("fixEngineLog.txt",true);

	fixEngineLogPtr-> addMessageTokens("FIX_Interface starting " + getUTCTimeString() + "\n");

	uint32_t lastIn;
	uint32_t lastOut;

	secFeeRate = 0.0;
	executionFeeRate = 0.0;
	readRates("rates.txt");

	heartBeatInterval = 14;

	
	ifstream inSeqStream("nextIncomingSeq.txt");
	inSeqStream >> lastIn;

	if( !inSeqStream.fail() && ( lastIn > 0) ) {
		char msg[256];
		nextIncomingSeqNum = lastIn;
		sprintf(msg,"Setting next incoming sequence number to %u\n",
			nextIncomingSeqNum);
		fixEngineLogPtr->addMessageTokens(msg);
	}
	else {
		char msg[256];
		sprintf(msg,"Next incoming sequence number defaulting to 1\n");
		nextIncomingSeqNum = 1;
		fixEngineLogPtr->addMessageTokens(msg);

	}
	lastReceivedSeqNum = nextIncomingSeqNum-1;

	ifstream outSeqStream("nextOutgoingSeq.txt");
	outSeqStream >> lastOut;

	if( !outSeqStream.fail() && ( lastOut > 0) ) {
		char msg[256];
		nextOutgoingSeqNum = lastOut + 1000;
		sprintf(msg,"Setting next outgoing sequence number to %u\n",
			nextOutgoingSeqNum);
		nextCL_ID = nextOutgoingSeqNum;
		fixEngineLogPtr->addMessageTokens(msg);
	}
	else {
		char msg[256];
		sprintf(msg,"Next outgoing sequence number defaulting to 1\n");
		nextOutgoingSeqNum = 1;
		nextCL_ID = 1;
		fixEngineLogPtr->addMessageTokens(msg);

	}
	readTradeState();
	orderLogPtr = new SnapshotLogfile("orders");
}

FIX_Interface::~FIX_Interface() {
	if( persist_fd > -1 ) {
		munmap(persistPtr,persist_size);
		close(persist_fd);
	}
	fixLogPtr->_write();
	fixEngineLogPtr->_write();
	delete fixLogPtr;
	delete fixEngineLogPtr;
}

//54 = side ( 1=buy,2=sell,5=sellshort,6=sellShortExempt )

//40 = ordType ( 2 = limit, 1 = market, P = pegged )

void FIX_Interface::createLogonBody(char* _body,int _heartBeatInterval,bool _resetSequence) {
	//98 = encryptMethod
	//108 = heartBtInt

	string resetString;	
	if( _resetSequence ) resetString = "141=Y" + sep;
	sprintf(_body,"98=0%c108=%d%c%s",sep,_heartBeatInterval,sep,resetString.c_str());
	
}

void FIX_Interface::createHeartbeatBody(char* _body,std::string _testString) {

	std::string testResponsePhrase;
	if(_testString.size() > 0) testResponsePhrase = "122=" + _testString + sep;
	
	sprintf(_body,"%s",testResponsePhrase.c_str());
	
}

inline void FIX_Interface::createNewOrderBody(const Order& _order,char* _body,std::string _resendClause) {
	//11 = clOrdID
	//21 = handling instructs (1=autoprivate, 2=autopublic, 3=manual)
	//54 = side ( 1=buy,2=sell,5=sellshort,6=sellShortExempt )
	//55 = symbol
	//38 = ordQty
	//40 = ordType ( 2 = limit, 1 = market, P = pegged )
	//47 = A
	//60 = transact time UTC
	char priceClause[64];	
	priceClause[0]='\0';
	if( _order.ordType != '1' ) sprintf(priceClause,"44=%f%c",_order.price,sep);
	char securityExchange[16];
	securityExchange[0]='\0';
	if( _order.exchange == "NYSE" ) {
		sprintf(securityExchange,"207=N\1");
	}
	//need to check for side or ordType == '?' and handle
	sprintf(_body,"%s11=%u%c21=1%c55=%s%c54=%c%c38=%u%c40=%c%c47=A%c59=%c%c100=%s%c%s%s21=1%c",
		_resendClause.c_str(), _order.clOrdID,sep,sep,_order.symbol.c_str(),sep,
		_order.side,sep,_order.size,sep,_order.ordType,sep,sep,_order.TIF,sep,_order.exchange.c_str(),sep,priceClause,securityExchange,sep);
	
}

inline void FIX_Interface::createOrderCancelReplaceBody(const Order& _order,Order& _newOrder,char* _body) {
	char replaceClause[32];
	sprintf(replaceClause,"41=%u%c",_order.clOrdID,sep);
	createNewOrderBody(_newOrder,_body,replaceClause);
}

inline void FIX_Interface::createOrderCancelBody(const Order& _order,char* _body) {
	//41 = origClOrdID
	//11 = clOrdID
	//55 = symbol
	//54 = Side
	//60 = transactTime (initial)

	sprintf(_body,"41=%u%c11=%u%c55=%s%c54=%c%c",_order.clOrdID,sep,getNextOrderID(),sep,_order.symbol.c_str(),sep,_order.side,sep);
	
}

inline void FIX_Interface::handleRawMessage(char *_message) {
	//printMessage(_message);

	std::unordered_map<std::string,std::string> parsedMessageMap;
	std::string messageType;
	uint32_t messageSeqNo;
	parseMessage(_message, parsedMessageMap, messageType,messageSeqNo);

	handleParsedMessage(parsedMessageMap,messageType,messageSeqNo);

}

bool FIX_Interface::sendMessage(char* _message, std::string _messageType,char _possDup,uint32_t _seqNo,std::string _sendingTimeString) {

	//FIX_MessageEnvelope fme;
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME,&ts);
	//fme.secs = ts.tv_sec;
	//fme.nanos = ts.tv_nsec;
	
	std::string sendingTimeString = (_sendingTimeString.empty()) ? getUTCTimeString() : _sendingTimeString;

	char buf[256];

	pthread_mutex_lock(&socketMutex);

	uint32_t seqNo = ( _seqNo > 0 ) ? _seqNo : nextOutgoingSeqNum;

	sprintf(buf,"35=%s%c49=%s%c50=%s%c56=%s%c34=%u%c43=%c%c52=%s%c1=%s%c%s",_messageType.c_str(),sep,senderCompID.c_str(),sep,senderSubID.c_str(),sep,targetCompID.c_str(),sep,seqNo,sep,_possDup,sep,sendingTimeString.c_str(),sep,account.c_str(),sep,_message);

	int bodyLength(strlen(buf));

	char buf2[256];
	sprintf(buf2,"8=FIX.4.2%c9=%d%c%s",sep,bodyLength,sep,buf);

	int checksum(calcChecksum(buf2));

	sprintf(_message,"%s10=%03d%c",buf2,checksum,sep);

	//send it!
	if(socketSendMessage(_message)) {
		if( _seqNo < 1 ) ++nextOutgoingSeqNum;

		//outgoingMessages.insert(outgoingMessages.end(),fme);
		
		fixLogPtr->addMessageTokens("O ",_message,"\n");
		lastSendTime = ts;
		pthread_mutex_unlock(&socketMutex);
		return true;
	}
	pthread_mutex_unlock(&socketMutex);
	return false;

}

void FIX_Interface::printMessage(char* _message) {
	int i(0);
	while(_message[i] != '\0') {
		if(_message[i] == (char)1)
			fputc('|',stdout);
		else 
			fputc(_message[i],stdout);

		++i;
	}
	fputc('\n',stdout);

}

bool FIX_Interface::logon() {
	if( sim ) return true;
		
	char messageBuffer[256];
	createLogonBody(messageBuffer,heartBeatInterval,false);
	if( sendMessage(messageBuffer,"A",'N') ) {
		return true;
		status = FS_LOGON;
	}
	return false;
}

void FIX_Interface::massCancel() {
	//disable for now
	return;

	status = FS_MASSCANCEL;
	char messageBuffer[256];
	createMassCancelBody(messageBuffer);
	sendMessage(messageBuffer,"q",'N');
}

inline int FIX_Interface::calcChecksum(char* _message) {
	int i(0);
	int checksum(0);

	while(_message[i] != '\0') {
		checksum += (unsigned int)_message[i++];
	}

	return (checksum % 256);
}

inline int FIX_Interface::calcChecksum(char* _message,long bufLen) {
	int checksum(0);

	for(long i = 0L; i < bufLen; checksum += (unsigned int)_message[i++]);

	return (checksum % 256);
}

inline uint32_t FIX_Interface::getNextOrderID(void) {
	return nextCL_ID++;
}

inline std::string FIX_Interface::getUTCTimeString(void) {
        char timeString[17];
        struct tm _myTm;

	time_t now(time(0));
	gmtime_r(&now,&_myTm);
	sprintf(timeString,"%04d%02d%02d-%02d:%02d:%02d",_myTm.tm_year + 1900,_myTm.tm_mon + 1,_myTm.tm_mday, _myTm.tm_hour,_myTm.tm_min,_myTm.tm_sec);
        return timeString;
}

void FIX_Interface::parseMessage(char* _message,std::unordered_map<std::string, std::string>& _parsedMessageMap,std::string& _messageType,uint32_t& _messageSeqNo) {
	char *messageContext;
	char *nullPointer(NULL);

	char* thisToken = strtok_r(_message,"\1",&messageContext);

  	while (true) {
  		thisToken = strtok_r(nullPointer,"\1",&messageContext);
		if( thisToken == NULL ) break;

		char *fieldContext;

		char* fieldTag = strtok_r(thisToken,"=",&fieldContext);
		if( fieldTag != NULL ) {
			char* fieldValue = strtok_r(nullPointer,"=",&fieldContext);
			//std::cout << "PARSE: " <<  fieldTag << "==";	
			if( fieldValue != NULL ) {
				//std::cout << fieldValue << std::endl;	
				_parsedMessageMap[fieldTag] = fieldValue;
				if( !strcmp(fieldTag,"35") ) _messageType = fieldValue;
				if( !strcmp(fieldTag,"34") ) _messageSeqNo = atoi(fieldValue);
			}
			else {
				//std::cout << std::endl;
				_parsedMessageMap[fieldTag] = "";
			}
		}
  	}

}

void FIX_Interface::readSocketThread() {
  bool loop = true;
  while ( loop ) {
      uint32_t notConnectedCount = 0;

      if( !connected ) {
		usleep(500000);
		notConnectedCount++;
		if((notConnectedCount % 20) == 0) {
			std::cout << "Not Connected: " << notConnectedCount << std::endl;
		}
		continue;
      }
      notConnectedCount = 0;

      if( sim ) {
	//loop through orders
	//confirm
	//give executions
	//give outs
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME,&ts);
	lastReceiveTime = ts;
	OSMI p = tradeStateMap.begin();
	for(; p != tradeStateMap.end(); ++p) {
		p->second.lock(); 
		OVI op = p->second.orderVector.begin();
		for(; op != p->second.orderVector.end(); ++op) {
			if( op->cancelTime.tv_sec == 0  ) {
				//execute
				long lastQty = op->size;
				if ( op->side != OS_BUY ) lastQty = -lastQty;
				handleExecution(p,lastQty,op->price,op->exchange,false);

			}
		}
		p->second.orderVector.clear();
		p->second.unlock(); 
	}
	sleep(1);
	continue;
      }

      char buf[512] = {'\0'};
      char* currentHeader = buf;
      const size_t firstReadSize = 20;

      int nRetCode = 0;
      int headerReadSize = 0;
      while ( headerReadSize < firstReadSize ) {
	  nRetCode = recv(sock, currentHeader,
		firstReadSize-headerReadSize , 0);

	  if (nRetCode ==-1) {
	      int nErrCode = errno;
	      if ( (nErrCode != ETIMEDOUT) && ( nErrCode != EINTR) && ( nErrCode != ECHILD) * ( nErrCode != ERESTART) ) {
		  char tmpbuf[32];
		  sprintf(tmpbuf,"SOCKET_ERROR errno=%d\n",nErrCode);
		  fixEngineLogPtr->addMessageTokens(tmpbuf);
		  disconnect();
		  break;
		}

	    }
	  else if ( nRetCode == 0 ) {
		fixEngineLogPtr->addMessageTokens("SOCKET_ERROR ret=0\n");
	      disconnect();
	      break;
	    }
	  if ( nRetCode > 0 ) {
	      currentHeader += nRetCode;
	      headerReadSize += nRetCode;
	    }
	}

     //if( headerReadSize > 0 ) {
//	char tmpbuf[64];
//	sprintf(tmpbuf,"Received Header : %s\n",buf);
//	fixEngineLogPtr->addMessageTokens(tmpbuf);
 //    }

      if( !connected ) continue;

      //buf[headerSize] = '\0';//shouldn't be necessary
      char* lengthString = strstr(buf, "9=");
      if( lengthString == NULL ) {
		  //didn't find the body length tag
		  std::cout << "Couldn't find 9= tag" << std::endl;
		  disconnect();
		  continue;
      }
      //++lengthString;
      char* terminator = strstr(lengthString, "\1");
      if( terminator == NULL ) {
		  //didn't find a field separator to end the body length field
		  std::cout << "Unable to find message size tag terminator" << std::endl;
		  disconnect();
		  continue;
      }
      if( terminator > &buf[firstReadSize] ) {
		  //didn't find it in header
		  std::cout << "9= tag didn't terminate in header" << std::endl;
		  disconnect();
		  continue;
      }
      *terminator = '\0';
      int size = atoi(lengthString+2) + 7; //additional 7 for the checksum field
      //std::cout << "Calculated size: " << size << std::endl;
      *terminator = '\1';//put back original value
      if( size > 512 ) {
		  //or whatever our max message size will be
		  std::cout << "message too large " << size << std::endl;
		  disconnect();
		  continue;
      }
      char* currentPointer = &buf[firstReadSize];

      size_t headerSize = terminator - buf + 1;
      size_t readSize = firstReadSize - headerSize;
      while ( readSize < size ) {
	  nRetCode = recv(sock, currentPointer,size-readSize , 0);
	  //char logbuf[size + 64];
	  //sprintf(logbuf," Received Intermediate Message : %s\n",buf);
	  //fixEngineLogPtr->addMessageTokens(logbuf);
	  if (nRetCode ==-1) {
	      int nErrCode = errno;
	      if ( errno != ETIMEDOUT) {
		  fixEngineLogPtr->addMessageTokens("SOCKET_ERROR\n");
		  disconnect();
		  break;
		}
	    }
	  else if ( nRetCode == 0 )
	    {
		fixEngineLogPtr->addMessageTokens("SOCKET_ERROR\n");
	      disconnect();
	    }
	  if ( nRetCode > 0 )
	    {
	      currentPointer += nRetCode;
	      readSize += nRetCode;
	    }
	}//while

      size_t sizeWithoutFooter = size + headerSize - 7;
      int checksum = calcChecksum(buf,sizeWithoutFooter);

      char expectedFooter[8];
      sprintf(expectedFooter,"10=%03d\1",checksum);

      char* footer = buf + sizeWithoutFooter;
      if( strcmp(expectedFooter,footer) ) {
		  //checksum mismatch
		fixEngineLogPtr->addMessageTokens("ERROR: DISCARDING: BAD_CHECKSUM: ",buf,"\n");
		continue;
      }


	char logbuf[size+64];
	sprintf(logbuf," Received Full Message : %s\n",buf);
	fixEngineLogPtr->addMessageTokens(logbuf);

	fixLogPtr->addMessageTokens("I ",buf,"\n");

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME,&ts);
	lastReceiveTime = ts;

      //return the message for processing
	handleRawMessage(buf);

    }//read socket loop

}

bool FIX_Interface::initConnection() {

if( sim ) {
    connected = true;
    std::cout << "SIM start connection." << std::endl;
    startThreads();

    return true;
}
  bool bRet = false;
  int nRetCode = 0;

  //Create a client side socket
  sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock == -1) {
    nRetCode = errno;
    disconnect();
    fixEngineLogPtr->addMessageTokens("FIX_Interface::connectSocket() FAILED\n");
    return false;
  }

  //Set the Receive Timeout.
  int nRxTimeout = 1000 * 1000; //Provides a 10 second timeout.

  nRetCode = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
			reinterpret_cast<char *>(&nRxTimeout), sizeof(nRxTimeout));
  bool tcpNoDelay = true;
  nRetCode = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
			reinterpret_cast<char *>(&tcpNoDelay), sizeof(int) );

  char szMsg2[64];
  sprintf( szMsg2, "TCP NO DELAY FLAG : retCode=%d\n",nRetCode );
  fixEngineLogPtr->addMessageTokens(szMsg2);

  int Zero = 65536;
  nRetCode = setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
			reinterpret_cast<char *>(&Zero), sizeof(int) );

  if (nRetCode !=0) {
    int nErrCode = errno;

    char szMsg2[64];
    sprintf( szMsg2, "Closing Socket Error Code: %d\n",nErrCode );
    fixEngineLogPtr->addMessageTokens(szMsg2);

    disconnect();
  }

  sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr=inet_addr(hostIP.c_str());
  serverAddr.sin_port = htons(hostPort);

  //Attempt to connect to the server side socket.
  nRetCode = connect(sock, reinterpret_cast<sockaddr *>(&serverAddr), sizeof(serverAddr));

  if (nRetCode !=0 ) {
      disconnect();
      char msgbuf[256];
      sprintf(msgbuf,"FIX_Interface::connectSocket() WARNING WARNING CANNOT CONNECT TO FIX SERVER. nret=[%d] [host=%s,port=%d] errno=[%d]!!!!!!!\n",
	      nRetCode,
	      hostIP.c_str(),hostPort,
	      errno);
	fixEngineLogPtr->addMessageTokens(msgbuf);
 
      return false;

    }

    connected = true;

    startThreads();

    return true;

}

inline bool FIX_Interface::socketSendMessage(char* msg) {

	bool result = true;
	int nRetCode  = 0;
	try {

		nRetCode = send(sock, msg, strlen(msg), 0);
		if (nRetCode ==-1) {
			int nErrCode = errno;
			result = false;
			char msg[256];
			sprintf(msg,"Unable to send FIX message on socket: ERRNO=%d\n",nErrCode);
			fixEngineLogPtr->addMessageTokens(msg);

			disconnect();

		}
	}
	catch (...)
	{
		throw;
	}

	// Release ownership of the critical section
	return result;
}

void FIX_Interface::closeSocket(int& sock) {
  fixEngineLogPtr->addMessageTokens("SOCKET_DISCONNECTING\n");
  connected = false;
  close(sock);
}

void FIX_Interface::logWriter() {
	while(writeLogs) {
		fixLogPtr->_write();
		fixEngineLogPtr->_write();
	}
}

void FIX_Interface::resendMessages(uint32_t _beginMessage, uint32_t _endMessage ) {

	setStatus(FS_RESEND);
	uint32_t begin = max((uint32_t)1,_beginMessage);

	pthread_mutex_lock(&socketMutex);
	uint32_t end = (_endMessage > 0) ? _endMessage : nextOutgoingSeqNum - 1;
	pthread_mutex_unlock(&socketMutex);

	//for the time being, we are just going to send a gap fill for the entire sequence
	//and cancel all orders
	
	sendGapFill(begin,end);
	//massCancel();
}

void FIX_Interface::createMassCancelBody(char* _body) {
	sprintf(_body,"11=%u%c530=7%c",getNextOrderID(),sep,sep);
}

void FIX_Interface::createSequenceReset(char* _body,bool _gapFill,uint32_t _newSequenceNum) {
	char YN = ( _gapFill ) ? 'Y' : 'N';
	sprintf(_body,"36=%u%c123=%c%c43=Y%c",_newSequenceNum,sep,YN,sep,sep);
}

void FIX_Interface::createResendRequest(char* _body,uint32_t _begin,uint32_t _end) {
	sprintf(_body,"7=%u%c16=%u%c",_begin,sep,_end,sep);
}

void FIX_Interface::createReject(char* _body,uint32_t _rejectedSeqNo, std::string _messageType, uint16_t _reason,const std::string& _text) {
	sprintf(_body,"45=%u%c372=%s%c373=%u%c58=%s%c",_rejectedSeqNo,sep,_messageType.c_str(),sep,_reason,sep,_text.c_str(),sep);
}

void FIX_Interface::setStatus(FIX_STATUS _status ) {
	if( status != _status ) {
		status = _status;
		char logbuf[64];
		sprintf(logbuf,"STATUS: %s\n",getStatusString(status).c_str());
		fixEngineLogPtr->addMessageTokens(logbuf);
	}
}

std::string FIX_Interface::getStatusString(FIX_STATUS _status) {
	string ss;
	switch(_status) {
		case FS_SUSPENDED:
			ss = "SUSPENDED";
			break;
		case FS_NORMAL:
			ss = "NORMAL";
			break;
		case FS_LOGON:
			ss = "LOGON";
			break;
		case FS_MASSCANCEL:
			ss = "MASSCANCEL";
			break;
		case FS_RESEND:
			ss = "RESEND";
			break;
		case FS_RETRAN:
			ss = "RETRAN";
			break;
		case FS_DISCONNECTED:
			ss = "DISCONNECTED";
			break;
		default:
			ss = "UNKNOWN";
			break;
	}
	return ss;
}

bool FIX_Interface::sendOrder(Order& _order,OSMI _context) {
	char message[256];
	_order.clOrdID = getNextOrderID();
	if( (_order.side == OS_SELL) && (_context->second.tradeState.position - _context->second.tradeState.sellSharesOutstanding - _order.size < 0) ) _order.side=OS_SELLSHORT;
	if( (_order.side==OS_SELLSHORT) && (!_context->second.tradeState.easyToBorrow)) return true;
	createNewOrderBody(_order,message);
	struct timespec now;
	clock_gettime(CLOCK_REALTIME,&now);
	_order.transactTime = now;
	_order.sendTimeUTC = getUTCTimeString(); //need to make this just a formatting call

	_context->second.lock();
	_context->second.orderVector.push_back(_order);	
	newOrderStatsUpdate(_context, _order );
	_context->second.unlock();

	if(!sim) {
	if(!sendMessage(message,"D",'N',0,_order.sendTimeUTC)) {
		_context->second.lock();
		removeOrderFromVector(_context->second.orderVector,_order.clOrdID);
		char buf[256];
		sprintf(buf,"ERROR: SEND_MSG_FAILED: %s NEW_ORDER %u\n",_context->second.tradeState.symbol,
			_order.clOrdID);
		fixEngineLogPtr->addMessageTokens(buf);
		_context->second.unlock();
		return false;
	} 
	}//sim
	_order.display(fixEngineLogPtr,"SEND_ORDER: ");
	return true;
}

bool FIX_Interface::cancelOrder(Order& _order, OSMI _context) {
	char message[256];
	createOrderCancelBody( _order,message);
	struct timespec now;
	clock_gettime(CLOCK_REALTIME,&now);
	//timespec tmp;
	OVI p;
	bool markedCanceled = false;
	_context->second.lock();
	bool found = locateOrderInVector(_context->second.orderVector,_order.clOrdID,p);
	bool confirmed = p->confirmed;
	if( found && confirmed && (p->cancelTime.tv_sec==0)) {
		markedCanceled = true;
		p->cancelTime = now;
	}
	_context->second.unlock();
	if( !markedCanceled ) return false;
	if(!sim) {
	if(!sendMessage(message,"F",'N')) {
		vector<Order>::iterator p;
		_context->second.lock();
		bool found = locateOrderInVector(_context->second.orderVector,_order.clOrdID,p);
		if( found ) {
			p->cancelTime = {0,0};
		}
		_context->second.unlock();
		return false;
	}
	}//sim
	_order.display(fixEngineLogPtr,"CANCEL_ORDER: ");
	return true;
}

bool FIX_Interface::cancelOrder(uint32_t _orderID) {
	//this function is not production quality
	OSMI p;
	OVI op;
	if( ! findOrder(_orderID,p,op) ) return false;
	return cancelOrder(*op,p);
}

bool FIX_Interface::cancelReplaceOrder(Order& _order,double _newPrice, long _newSize,OSMI _context) {
	char message[256];
	Order newOrder = _order;
	newOrder.clOrdID = getNextOrderID();
	newOrder.transactTime.tv_sec=0;
	newOrder.transactTime.tv_nsec=0;
	newOrder.ackTime.tv_sec=0;
	newOrder.ackTime.tv_nsec=0;
	newOrder.cancelTime.tv_sec=0;
	newOrder.cancelTime.tv_nsec=0;
	newOrder.confirmed=false;
	newOrder.takeLiquidity=false;
	newOrder.replaced = false;

	newOrder.price = _newPrice;
	newOrder.size = _newSize;
	newOrder.sendTimeUTC = getUTCTimeString();
		
	createOrderCancelReplaceBody(_order,newOrder,message);
	struct timespec now;
	clock_gettime(CLOCK_REALTIME,&now);
	newOrder.transactTime = now;

	_context->second.lock();

	OVI p;
	bool found = locateOrderInVector(_context->second.orderVector,_order.clOrdID,p);
	bool confirmed = p->confirmed;
	bool markedCanceled = false;
	if( found && confirmed && (p->cancelTime.tv_sec==0) ) {
		p->cancelTime = now;
		markedCanceled = true;
		p->replaced = true;
	}
	if(markedCanceled) { 
		_context->second.orderVector.push_back(newOrder);	
		newOrderStatsUpdate(_context, newOrder );
		char buf[256];
		sprintf(buf,"REPLACE: %s %u=>%u %d %d\n",
			newOrder.symbol.c_str(),
			_order.clOrdID,
			newOrder.clOrdID,
			newOrder.transactTime.tv_sec,
			newOrder.transactTime.tv_nsec
			);
		fixEngineLogPtr->addMessageTokens(buf);
	}
	else {
		_context->second.unlock();
		return false;
	}
	//timespec tmp;
	_context->second.unlock();
	if(!sim) {
	if(!sendMessage(message,"G",'N',0,newOrder.sendTimeUTC)) {
		_context->second.lock();
		removeOrderFromVector(_context->second.orderVector,newOrder.clOrdID);
		OVI p;
		bool found = locateOrderInVector(_context->second.orderVector,_order.clOrdID,p);
		if( found ) {
			p->cancelTime = {0,0};
			p->replaced = false;
		}
		_context->second.unlock();
		return false;
	}
	}//sim
	_order.display(fixEngineLogPtr,"CANCEL_ORDER: ");
	newOrder.display(fixEngineLogPtr,"SEND_ORDER: ");
	return true;
}

bool FIX_Interface::cancelReplaceOrder(uint32_t _orderID,double _newPrice, long _newSize) {
	OSMI _mapI;
	OVI _vectorI;
	bool found = findOrder(_orderID,_mapI,_vectorI);
	if( ! found ) return false;
	return cancelReplaceOrder(*_vectorI,_newPrice,_newSize,_mapI);
}

void FIX_Interface::sendGapFill(uint32_t _begin,uint32_t _end) {
	char message[256];
	createSequenceReset(message,true,_end+1);//true means gap fill
	sendMessage(message,"4",'N',_begin); // this message need to have seqno == _begin
}

void FIX_Interface::requestResend(uint32_t _begin,uint32_t _end) {
	char message[256];
	createResendRequest(message,_begin,_end);
	sendMessage(message,"2",'N');
}

void FIX_Interface::sendReject(uint32_t _rejectedSeqNo, std::string _messageType, uint16_t _reason, const std::string& _text) {
	char message[256];
	createReject(message,_rejectedSeqNo,_messageType,_reason,_text);
	sendMessage(message,"3",'N');

}

void FIX_Interface::disconnect() {
	keepAlive = false;
	if(sim) return;
	closeSocket(sock);
	setStatus(FS_DISCONNECTED);
}

bool FIX_Interface::getMessageTag(std::string _tag,std::string& _value, unordered_map<std::string, std::string>& _parsedMessageMap) {
	unordered_map<std::string, std::string>::iterator p = _parsedMessageMap.find(_tag);
	if( p != _parsedMessageMap.end() ) {
		_value = p->second;
		return true;
	}
	return false; 
}

void FIX_Interface::handleParsedMessage(std::unordered_map<std::string,std::string>& _parsedMessageMap,std::string _messageType,uint32_t _messageSeqNo) {

	if( ! goodSeqNo(_messageSeqNo,_messageType) ) return;

	lastReceivedSeqNum = _messageSeqNo;
	std::string posDup;
	bool havePosDup = getMessageTag("43",posDup,_parsedMessageMap);

	//if( (posDup == "Y") && (_messageType != "8") ) return;

	if(_messageType == "8" ) {
		//handleExecutionReport
		//std::cout << "RECVD execution report" << std::endl;
		std::string statusString;
		bool haveStatus = getMessageTag("39",statusString,_parsedMessageMap);
		if( !haveStatus || (statusString.length() < 1)) {
			std::cout << "EXECUTION_REPORT WITHOUT STATUS " << _messageSeqNo << std::endl;
			return;
		}
		std::string clOrdIDString;
		bool haveclOrderID = getMessageTag("11",clOrdIDString,_parsedMessageMap);
		std::string statusDetail = ordStatus2String(statusString[0]);
		std::string symbol;
		bool haveSymbol = getMessageTag("55",symbol,_parsedMessageMap);
		std::string priceString;
		bool havePriceString = getMessageTag("31",priceString,_parsedMessageMap);
		std::string sideString;
		bool haveSideString = getMessageTag("54",sideString,_parsedMessageMap);
		std::string text;
		bool haveText = getMessageTag("58",text,_parsedMessageMap);
		std::string cumQtyString;
		bool haveCumQty = getMessageTag("14",cumQtyString,_parsedMessageMap);
		std::string lastQtyString;
		bool havelastQty = getMessageTag("32",lastQtyString,_parsedMessageMap);
		std::string orderID;
		bool haveOrderID = getMessageTag("37",orderID,_parsedMessageMap);
		std::string transactTimeString;
		bool havetransactTime = getMessageTag("60",transactTimeString,_parsedMessageMap);
		std::string execTypeString;
		bool haveExecType = getMessageTag("150",execTypeString,_parsedMessageMap);

		uint32_t clOrdID = atoi(clOrdIDString.c_str());
	
		if( execTypeString.length() < 1 ) {
			setStatus(FS_SUSPENDED);
			char buf[128];
			sprintf(buf,"INVALID_EXEC_RPT: No EXECTYP: %s %u\n", clOrdIDString.c_str(), _messageSeqNo);
			fixEngineLogPtr->addMessageTokens(buf); 
			
			return;
		}
			
		char execType = execTypeString[0];

		//std::cout << "EXECTYPE: " << execType << std::endl;

		OSMI context;
		bool found = findTradeContext(symbol,context);
		if( !found ) {
			setStatus(FS_SUSPENDED);
			char buf[128];
			sprintf(buf,"UNKNOWN_SYMBOL: %s %s %u\n", symbol.c_str(),clOrdIDString.c_str(), _messageSeqNo);
			fixEngineLogPtr->addMessageTokens(buf); 
			return;
		}


		switch(execType) {
			case '0':
			{
				//confirm
				if( posDup == "Y" ) break;
				context->second.lock();	

				OVI op;
				if( ! locateOrderInVector(context->second.orderVector,clOrdID,op) ) {
					setStatus(FS_SUSPENDED);
					char buf[128];
					sprintf(buf,"UNKNOWN_ORD: %s %u\n", clOrdIDString.c_str(), _messageSeqNo);
					fixEngineLogPtr->addMessageTokens(buf); 
				}
				else {
					//op->ackTime = ...
					op->confirmed = true;
					context->second.tradeState.lastInSeqNo =  _messageSeqNo;
				}
				context->second.unlock();	
				break;
			}
			case '1':
			case '2':
			{
				//execution
				long lastQty = atoi(lastQtyString.c_str());
				if( sideString != "1" ) lastQty = -lastQty;
				double price = atof(priceString.c_str());

				context->second.lock();	
				std::string exchange;

				OVI op;
				if( ! locateOrderInVector(context->second.orderVector,clOrdID,op) ) {
					setStatus(FS_SUSPENDED);
					char buf[128];
					sprintf(buf,"UNKNOWN_ORD: %s %u\n", clOrdIDString.c_str(), _messageSeqNo);
					fixEngineLogPtr->addMessageTokens(buf); 
				}
				else {
					exchange = op->exchange;
					timespec cancelTime = op->cancelTime;
					bool replaced = op->replaced;
					
					if( execType == '2')  context->second.orderVector.erase(op);
					if( replaced ) removeStrandedOrder(context,cancelTime);
					//should update last execution time and position
				}
				handleExecution(context,lastQty,price,exchange,false);
				context->second.tradeState.lastInSeqNo =  _messageSeqNo;
				context->second.unlock();	
					
				char buf[128];
				std::string execID;
				bool takeLiquidity = false;
				bool haveExecID = getMessageTag("17",execID,_parsedMessageMap);
				sprintf(buf,"EXECUTION: %ld %s %f %s %s %d %s %s %s\n",lastQty,symbol.c_str(),price,transactTimeString.c_str(),exchange.c_str(),takeLiquidity,clOrdIDString.c_str(),execID.c_str(),orderID.c_str());
				fixEngineLogPtr->addMessageTokens(buf); 
				break;
			}
			case '3':
			case '4':
			case 'B':
			case 'C':
			{
				//out
				//find order and remove
				if( posDup == "Y" ) break;
				std::string origClOrdIDString;
				bool haveOrigClOrderID = getMessageTag("41",origClOrdIDString,_parsedMessageMap);
				uint32_t origClOrdID = atoi(origClOrdIDString.c_str());


				context->second.lock();	

				OVI op;
				if( ! locateOrderInVector(context->second.orderVector,origClOrdID,op) ) {
					setStatus(FS_SUSPENDED);
					char buf[128];
					sprintf(buf,"UNKNOWN_ORD: %s %u\n", origClOrdIDString.c_str(), _messageSeqNo);
					fixEngineLogPtr->addMessageTokens(buf); 
				}
				else {
					context->second.orderVector.erase(op);
					context->second.tradeState.lastInSeqNo =  _messageSeqNo;
				}
				context->second.unlock();	
				break;
			}
			case '5':
			{
				//replace
				//mark new confirmed
				//remove old
				if( posDup == "Y" ) break;
				std::string origClOrdIDString;
				bool haveOrigClOrderID = getMessageTag("41",origClOrdIDString,_parsedMessageMap);
				uint32_t origClOrdID = atoi(origClOrdIDString.c_str());

				//std::cout << "RECVD REPLACE orig=" << origClOrdID << " new=" << clOrdID << std::endl;


				context->second.lock();	

				OVI op;
				if( ! locateOrderInVector(context->second.orderVector,clOrdID,op) ) {
					setStatus(FS_SUSPENDED);
					char buf[128];
					sprintf(buf,"UNKNOWN_ORD: %s %u\n", clOrdIDString.c_str(), _messageSeqNo);
					fixEngineLogPtr->addMessageTokens(buf); 
				}
				else {
					context->second.tradeState.lastInSeqNo =  _messageSeqNo;
					op->confirmed = true;
				}

				if( ! locateOrderInVector(context->second.orderVector,origClOrdID,op) ) {
					setStatus(FS_SUSPENDED);
					char buf[128];
					sprintf(buf,"UNKNOWN_ORD: %s %u\n", origClOrdIDString.c_str(), _messageSeqNo);
					fixEngineLogPtr->addMessageTokens(buf); 
				}
				else {
					context->second.orderVector.erase(op);
				}
				context->second.unlock();
				break;
			}
			case '6':
			{
				//pending_cancel
				break;
			}
			case '7':
			case '8':
			case '9':
			{
				//rejected
				//remove order
				//mark security rejected ( no more orders )
				if( posDup == "Y" ) break;

				context->second.lock();	

				OVI op;
				if( ! locateOrderInVector(context->second.orderVector,clOrdID,op) ) {
					setStatus(FS_SUSPENDED);
					char buf[128];
					sprintf(buf,"UNKNOWN_ORD: %s %u\n", clOrdIDString.c_str(), _messageSeqNo);
					fixEngineLogPtr->addMessageTokens(buf); 
				}
				else {
					context->second.tradeState.rejected = true;
					context->second.tradeState.lastInSeqNo =  _messageSeqNo;
					context->second.orderVector.erase(op);
				}
				context->second.unlock();	
				break;
			}
			case 'A':
			{
				//pending new
				break;
			}
			case 'D':
			{
				//restated
				break;
			}
			case 'E':
			{
				//pending replace
				break;
			}
			default:
			//unknown
			break;
		}


		char buf[64];
		sprintf(buf,"ORD_STATUS: %s %s %s %s\n",clOrdIDString.c_str(),statusDetail.c_str(),text.c_str(),symbol.c_str());
		fixEngineLogPtr->addMessageTokens(buf); 
		//A whole lot of details
		return;
	}	

	if(_messageType == "9" ) {
		//I guess we didn't get an out--prepare for a loss
		/*
		OSMI context;
		bool found = findTradeContext(symbol,context);
		if( !found ) {
			setStatus(FS_SUSPENDED);
			char buf[128];
			sprintf(buf,"UNKNOWN_SYMBOL: %s %s %u\n", symbol.c_str(),clOrdIDString.c_str(), _messageSeqNo);
			fixEngineLogPtr->addMessageTokens(buf); 
			return;
		}
		*/
/*
		std::string symbol;
		bool haveSymbol = getMessageTag("55",symbol,_parsedMessageMap);
		OSMI context;
		bool found = findTradeContext(symbol,context);
		if( !found ) {
			setStatus(FS_SUSPENDED);
			char buf[128];
			sprintf(buf,"UNKNOWN_SYMBOL: %s %s %u\n", symbol.c_str(), _messageSeqNo);
			fixEngineLogPtr->addMessageTokens(buf); 
			return;
		}

		context->second.tradeState.lastExecutionTime = lastReceiveTime;
*/
		char buf[64];
		sprintf(buf,"CANCEL_REJECT: %u\n",_messageSeqNo);
		fixEngineLogPtr->addMessageTokens(buf);
		return;
	}	

	if(_messageType == "0" ) {
		//heartbeat
		char buf[64];
		sprintf(buf,"HEARTBEAT_RECVD\n");
		fixEngineLogPtr->addMessageTokens(buf);
		return;
	}	

	if(_messageType == "1" ) {
		std::string testReqID;
		bool haveBegin = getMessageTag("112",testReqID,_parsedMessageMap);
		char buf[64];
		sprintf(buf,"TEST_REQUEST %s\n",testReqID.c_str());
		fixEngineLogPtr->addMessageTokens(buf);
		sendHeartbeat(testReqID);
		goodSeqNo(_messageSeqNo,_messageType,true);
		return;
	}	

	if(_messageType == "2" ) {
		std::string beginString;
		bool haveBegin = getMessageTag("7",beginString,_parsedMessageMap);
		std::string endString;
		bool haveEnd = getMessageTag("16",endString,_parsedMessageMap);
		std::cout << "GOT RESEND! " << beginString << " " << endString << std::endl;
		if( !haveEnd || !haveBegin ) {
			char buf[64];
			sprintf(buf,"ERROR: Required Tag missing on resend request.  Disconnecting.\n");
			fixEngineLogPtr->addMessageTokens(buf);
			sendReject(_messageSeqNo,_messageType,1,buf);
			disconnect();
			return;
		}
		uint32_t beginNum = atoi(beginString.c_str());
		uint32_t endNum = atoi(endString.c_str());
		if( ( beginNum < 0 ) || (beginNum > nextOutgoingSeqNum-1) ||
			( endNum <= beginNum ) || (endNum > nextOutgoingSeqNum-1) ) {
			char buf[64];
			sprintf(buf,"ERROR: Invalid sequence numbers requested in resend. Disconnecting. b=%u e=%u ourLast=%u\n",beginNum,endNum,nextOutgoingSeqNum-1);
			fixEngineLogPtr->addMessageTokens(buf);
			sendReject(_messageSeqNo,_messageType,1,buf);
			disconnect();
			return;
		}
 
		resendMessages(beginNum, endNum );
		//after we satify their request, see if we need one as well
		if(!goodSeqNo(_messageSeqNo,_messageType,true)) setStatus(FS_SUSPENDED);

		return;
	}	

	if(_messageType == "3" ) {
		char buf[64];
		std::string refSeqNum;
		getMessageTag("45",refSeqNum,_parsedMessageMap); 
		std::string refTagID;
		getMessageTag("371",refTagID,_parsedMessageMap); 
		std::string refMsgType;
		getMessageTag("372",refMsgType,_parsedMessageMap); 
		std::string sessionRejectReason;
		getMessageTag("373",sessionRejectReason,_parsedMessageMap); 
		std::string text;
		getMessageTag("58",text,_parsedMessageMap); 
		sprintf(buf,"ERROR: Session Level Reject: seq=%s tagID=%s msgType=%s reason=%s text=%s Disconnecting.\n",
			refSeqNum.c_str(),
			refTagID.c_str(),
			refMsgType.c_str(),
			sessionRejectReason.c_str(),
			text.c_str()
			);

		fixEngineLogPtr->addMessageTokens(buf);
		disconnect();
		return;
	}	

	if( _messageType == "4" ) {
		//sequence reset
		handleSequenceReset(_parsedMessageMap,_messageType,_messageSeqNo);
		return;
	}

	if(_messageType == "5" ) {
		char buf[64];
		sprintf(buf,"ERROR: LOGOUT RECEIVED.  Disconnecting.\n");
		fixEngineLogPtr->addMessageTokens(buf);
		disconnect();
		return;
	}	


	if(_messageType == "r" ) {
		//mass cancel report
		if( status == FS_MASSCANCEL ) {
			char buf[64];
			sprintf(buf,"STATUS: MASS_CANCEL CONFIRMED.\n");
			fixEngineLogPtr->addMessageTokens(buf);
			setStatus(FS_SUSPENDED);
		}
		else {
			char buf[64];
			sprintf(buf,"ERROR: Unexpected MASS_CANCEL received.  Disconnecting.\n");
			fixEngineLogPtr->addMessageTokens(buf);
			setStatus(FS_SUSPENDED);
			//disconnect();
		}
		return;
	}	

	if(_messageType == "A" ) {
		fixEngineLogPtr->addMessageTokens("RECVD LOGON\n");
		if( status == FS_LOGON ) setStatus(FS_SUSPENDED);
		std::string resetSeqNumFlag;
		bool haveReset = getMessageTag("141",resetSeqNumFlag,_parsedMessageMap); 
		if( haveReset ) {
			uint32_t newSeqNo = atoi(resetSeqNumFlag.c_str());
			resetSeqNo(newSeqNo,_messageSeqNo,_messageType);
		}
				
		std::string heartBeatString;
		bool haveHeartBeat = getMessageTag("108",heartBeatString,_parsedMessageMap); 

		if( !haveHeartBeat ) fixEngineLogPtr->addMessageTokens("WARNING: No heartbeat interval in logon response.");
		else {
			uint32_t tmpHeartBeatInterval = atoi(heartBeatString.c_str());
			if( tmpHeartBeatInterval != heartBeatInterval ) {
				if( tmpHeartBeatInterval > 0 ) {
					char buf[64];
					sprintf(buf,"WARNING:  heartBeatInterval != requested.  %u != %u.  Using %u.\n",heartBeatInterval,tmpHeartBeatInterval,tmpHeartBeatInterval);
					fixEngineLogPtr->addMessageTokens(buf);
					heartBeatInterval = tmpHeartBeatInterval;
				}
				else {
					char buf[64];
					sprintf(buf,"WARNING:  heartBeatInterval < 0.  %u Using %u.\n",tmpHeartBeatInterval,heartBeatInterval);
					fixEngineLogPtr->addMessageTokens(buf);
				}
			}
		}
		goodSeqNo(_messageSeqNo,_messageType,true);

		return;
	}

	char buf[64];
	sprintf(buf,"WARNING: Unknown message type=%s seqNo=%u\n",_messageType.c_str(),_messageSeqNo);
	fixEngineLogPtr->addMessageTokens(buf);
	disconnect();
}

bool FIX_Interface::sendHeartbeat(std::string _testString) {
	if(sim) return true;
	char message[256];
	createHeartbeatBody(message,_testString);
	return sendMessage(message,"0",'N');
}

void FIX_Interface::heartbeatThread() {
	while(keepAlive) {
		if( connected && !sim ) {
			struct timespec now;
			clock_gettime(CLOCK_REALTIME,&now);
			timespec dt = diff(lastSendTime, now);
			//std::cout << "DIFF " << dt.tv_sec << " " << heartBeatInterval << std::endl;
			if( dt.tv_sec > heartBeatInterval - 1 ) sendHeartbeat("");
		}
		//std::cout << "ABOUT TO PERSIST." << std::endl;
		persist();
		usleep(500000);
		fixLogPtr->_write();
		fixEngineLogPtr->_write();
	}
}

bool FIX_Interface::goodSeqNo(uint32_t _messageSeqNo, std::string _messageType,bool _overRide ) {
	if( _messageSeqNo > messageHighWaterMark ) messageHighWaterMark = _messageSeqNo;
	//resend request or sequence reset
	//std::cout << "BEFORE " << _overRide << " " << _messageSeqNo << " " << _messageType << " " << nextIncomingSeqNum << std::endl;
	if((!_overRide) &&  ((_messageType == "2") || (_messageType == "4") || (_messageType == "A") || (_messageType == "1") ) ) return true;  //resend request or sequence reset
	if( _messageSeqNo == nextIncomingSeqNum ) {
		if( (_messageSeqNo == messageHighWaterMark) && ( status == FS_RETRAN) ) setStatus(FS_SUSPENDED);
	//std::cout << "AFTER " << _overRide << " " << _messageSeqNo << " " << _messageType << std::endl;
		nextIncomingSeqNum++;  //expect more
		lastReceivedSeqNum = _messageSeqNo;
		retranRequestCount = 0;
		return true;
	}
	if( _messageSeqNo < nextIncomingSeqNum ) {
			char buf[64];
			sprintf(buf,"ERROR: SeqNo < expected: %u < %u Disconnecting.\n",_messageSeqNo,nextIncomingSeqNum);
			fixEngineLogPtr->addMessageTokens(buf);
			sendReject(_messageSeqNo,_messageType,5,buf);
			disconnect();
			return false;
		}
	else {
		//too big.  We are missing messages
		if( _messageSeqNo > messageHighWaterMark ) messageHighWaterMark = _messageSeqNo;
		if( retranRequestCount < maxRetranRequests ) {
			char buf[64];
			sprintf(buf,"WARNING: SeqNo > expected: %u > %u Requesting resend %u-\n",_messageSeqNo,nextIncomingSeqNum,nextIncomingSeqNum,0);
			fixEngineLogPtr->addMessageTokens(buf);
			setStatus(FS_RETRAN);
			requestResend(nextIncomingSeqNum,0);
			retranRequestCount++;
			return false;
		}
		else {
			char buf[64];
			sprintf(buf,"ERROR: Too Many consecutive out of sequence messages %u.  Disconnecting.\n",retranRequestCount);
			fixEngineLogPtr->addMessageTokens(buf);
			sendReject(_messageSeqNo,_messageType,5,buf);
			disconnect();
			return false;
		}
	}
	return false;
}

void FIX_Interface::handleSequenceReset(std::unordered_map<std::string,std::string>& _parsedMessageMap,std::string _messageType,uint32_t _messageSeqNo) {

	std::string gapFill;
	bool haveGapFill = getMessageTag("123",gapFill,_parsedMessageMap); 
	std::string newSeqNoString;
	bool haveNewSeqNoString = getMessageTag("36",newSeqNoString,_parsedMessageMap); 
	uint32_t newSeqNo = 0;
	if( haveNewSeqNoString ) newSeqNo = atoi(newSeqNoString.c_str());
	char buf[128];
	sprintf(buf,"RCV_RESEND: seqno=%u new=%u gapFill=%s\n",_messageSeqNo,newSeqNo,gapFill.c_str());
	fixEngineLogPtr->addMessageTokens(buf);
	if( haveGapFill && ( gapFill == "Y" )) {
		bool needResend = (_messageSeqNo != nextIncomingSeqNum);
		bool resetOkay = resetSeqNo(newSeqNo,_messageSeqNo,_messageType);
		char buf[128];
		sprintf(buf,"PROC_RESEND: needResend=%d resetOK=%d\n",needResend,resetOkay);
		fixEngineLogPtr->addMessageTokens(buf);
		if( needResend && resetOkay ) requestResend(nextIncomingSeqNum,0);
	}
	else {
		resetSeqNo(newSeqNo,_messageSeqNo,_messageType);
	}
}

void FIX_Interface::startThreads() {
	m_readThread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&FIX_Interface::readSocketThread, this)));
	m_heartbeatThread = boost::shared_ptr<boost::thread>(new boost::thread(boost::bind(&FIX_Interface::heartbeatThread, this)));
}

void FIX_Interface::listOrders() {
	OSMI p = tradeStateMap.begin();
	for(; p != tradeStateMap.end(); ++p) {
		p->second.lock(); 
		OVI op = p->second.orderVector.begin();
		char buf[1024];
		for(; op != p->second.orderVector.end(); ++op) {
			op->display(buf);
			orderLogPtr->write(buf);
		}
	
		p->second.unlock();
	}
	orderLogPtr->complete();
}

bool FIX_Interface::findOrder(uint32_t _orderID,OSMI& _mapI,OVI& _vectorI) {
	//this function is for the CLI
	//it is not production quality
	OSMI p = tradeStateMap.begin();
	for(; p != tradeStateMap.end(); ++p) {
		p->second.lock(); 
		OVI op = p->second.orderVector.begin();
		for(; op != p->second.orderVector.end(); ++op) {
			if( op->clOrdID == _orderID) {
				_mapI = p;
				_vectorI = op;
				p->second.unlock();
				return true;
			}
			p->second.unlock();
		}
	}
	return false;
}

bool FIX_Interface::resetSeqNo(uint32_t _newSeqNo,uint32_t _messageSeqNo,std::string _messageType) {
	if( _newSeqNo >= nextIncomingSeqNum ) {
		char buf[128];
		sprintf(buf,"WARNING: Skipping message %u - %u.\n",nextIncomingSeqNum,_newSeqNo-1);
		fixEngineLogPtr->addMessageTokens(buf);
		if( status == FS_NORMAL ) setStatus(FS_SUSPENDED);
		nextIncomingSeqNum = _newSeqNo;
		messageHighWaterMark = _newSeqNo;	
		lastReceivedSeqNum = nextIncomingSeqNum-1;
		return true;
	}
	else {
		char buf[128];
		sprintf(buf,"ERROR: Attempt to set invalid sequence Number %u.  Disconnecting.\n",_newSeqNo);
		fixEngineLogPtr->addMessageTokens(buf);
		sendReject(_messageSeqNo,_messageType,5,buf);
		disconnect();
	}
	return false;
}

void FIX_Interface::getTradeState(OSMI _context,TradeState& _tradeStateRef,vector<Order>& _orderVectorRef) {
	
	_context->second.getTradeState(_tradeStateRef,_orderVectorRef);
}

OSMI FIX_Interface::getTradeContext(std::string _symbol) {
	OSMI p = tradeStateMap.find(_symbol);
	if( p != tradeStateMap.end() ) return p;
	tradeStateMap[_symbol];
	p = tradeStateMap.find(_symbol);
	strcpy(p->second.tradeState.symbol,_symbol.c_str());
	return tradeStateMap.find(_symbol);
}

inline bool FIX_Interface::findTradeContext(std::string _symbol,OSMI& _mapI) {
	_mapI = tradeStateMap.find(_symbol);
	return ( _mapI != tradeStateMap.end() );
}

inline bool FIX_Interface::locateOrderInVector(vector<Order>& _orderVector,uint32_t _ordID,OVI& _foundIterator) {
	_foundIterator = _orderVector.begin();
	for(; _foundIterator != _orderVector.end(); ++_foundIterator) {
		if( _foundIterator->clOrdID == _ordID ) {
			return true;
		}
	} 
	return false;
}

inline bool FIX_Interface::removeOrderFromVector(vector<Order>& _orderVector,uint32_t _ordID) {
	vector<Order>::iterator p;
	bool found = locateOrderInVector(_orderVector,_ordID,p);
	if( found ) {
		_orderVector.erase(p);
		return true;
	}
	return false;
}

void FIX_Interface::newOrderStatsUpdate(OSMI _context, Order& _orderRef ) {
	double dollars = _orderRef.size * _orderRef.price;
	if( _orderRef.side != OS_BUY ) {
		_context->second.tradeState.orderedSellShares += _orderRef.size;
		_context->second.tradeState.orderedBuyDollars += _orderRef.size * _orderRef.price;
	}
	else {
		_context->second.tradeState.orderedBuyShares += _orderRef.size;
		_context->second.tradeState.orderedSellDollars += _orderRef.size * _orderRef.price;
	}
}

void FIX_Interface::setOvernightPostion(OSMI _context, long _overnightPosition) {
	_context->second.lock();
	_context->second.tradeState.overnightPosition = _overnightPosition;
	_context->second.tradeState.position = _overnightPosition +
		_context->second.tradeState.executedBuyShares - 
		_context->second.tradeState.executedSellShares;
	_context->second.unlock();
}

inline void FIX_Interface::handleExecution(OSMI _context,long _lastQty,double _price,std::string _exchange, bool _takeLiquidity)  {
	long shares = abs(_lastQty);
	double dollars = static_cast<double>(shares * _price);
	double rebateRate = ( _takeLiquidity ) ? takeMap[_exchange] : provideMap[_exchange];
	_context->second.tradeState.rebateDollars += shares * rebateRate;
	_context->second.tradeState.executionFeeDollars += shares * executionFeeRate;
	if( _lastQty < 0 ) {
		_context->second.tradeState.executedSellShares += shares;
		_context->second.tradeState.executedSellDollars += dollars;
		_context->second.tradeState.secFeeDollars += dollars * secFeeRate;
	}
	else {
		_context->second.tradeState.executedBuyShares += shares;
		_context->second.tradeState.executedBuyDollars += dollars;
	}
	_context->second.tradeState.position = 
		_context->second.tradeState.overnightPosition +
		_context->second.tradeState.executedBuyShares - 
		_context->second.tradeState.executedSellShares;

	_context->second.tradeState.lastExecutionTime = lastReceiveTime;
}

void FIX_Interface::readTradeState() {

	uint32_t tmpLastInSeqNo(0);

        if ((persist_fd = open("tradeState.dat", O_RDWR|O_CREAT, S_IRUSR | S_IWUSR)) == -1) {
		fixEngineLogPtr->addMessageTokens("ERROR: Unable to open tradeState.dat\n");
		exit(1);
	}
	uint32_t numRecords = 0;
	uint32_t tmpNextOutgoingSeqNo=1;
	int resLastOut = read(persist_fd,&tmpNextOutgoingSeqNo,sizeof(uint32_t));
	if( resLastOut < 1 ) {
		fixEngineLogPtr->addMessageTokens("Unable to next Outgoing SeqNo from tradeState.dat\n");
	}
	int res = read(persist_fd,&numRecords,sizeof(uint32_t));
	if( res < 1 ) {
		fixEngineLogPtr->addMessageTokens("Unable to read # of records from tradeState.dat\n");
	}
	if( numRecords > 1000 ) {
		fixEngineLogPtr->addMessageTokens("WARNING: tradeState records > 1000\n");
	}
	uint32_t i;
	TradeState tmpTradeState;
	for( i = 0; i< numRecords;++i) {
		int res = read(persist_fd,(void*)&tmpTradeState,sizeof(TradeState));
		if( res < sizeof(TradeState) ) {
			char numString[16];
			sprintf(numString,"%u",i+1);
			fixEngineLogPtr->addMessageTokens("ERROR: tradeState.dat Unable read record #: %s",numString);
			break;
		}
		else {
			if( tmpTradeState.lastInSeqNo > tmpLastInSeqNo ) tmpLastInSeqNo = tmpTradeState.lastInSeqNo;
			tradeStateMap[tmpTradeState.symbol].tradeState = tmpTradeState;			
		}
	}
	if( resLastOut < 1 ) {
		write(persist_fd,&tmpNextOutgoingSeqNo,sizeof(uint32_t));
	}
	if( numRecords < 1 ) {
		write(persist_fd,&numRecords,sizeof(uint32_t));
	}
	for(; i < 1000;++i) {
		TradeState blankTradeState;
		blankTradeState.position = 6666;
		write(persist_fd,&blankTradeState,sizeof(TradeState));
	}
	fsync(persist_fd);
	persist_size = sizeof(uint32_t) + sizeof(uint32_t) + 1000 * sizeof(TradeState);
        persistPtr = (char*)mmap(NULL, persist_size , PROT_WRITE, MAP_FILE|MAP_SHARED, persist_fd, 0);
 
        if (persistPtr == MAP_FAILED) {
		fixEngineLogPtr->addMessageTokens("ERROR: mmap for tradeStatedat\n");
		exit(1);
	}
	std::cout << "RECOVER: in=" << tmpLastInSeqNo << " out=" << tmpNextOutgoingSeqNo << std::endl;
	//if( nextIncomingSeqNum < 2 ) nextIncomingSeqNum = tmpLastInSeqNo + 1;
	//if( nextOutgoingSeqNum < 2 ) nextOutgoingSeqNum = tmpLastOutSeqNo + 1000;
	nextIncomingSeqNum = tmpLastInSeqNo + 1;
	nextOutgoingSeqNum = tmpNextOutgoingSeqNo + 8000;
	std::cout << "START_SEQNOS: in=" << nextIncomingSeqNum << " out=" << nextOutgoingSeqNum << std::endl;
}

void FIX_Interface::persist() {
	uint32_t maxI=tradeStateMap.size();
	uint32_t* sizePtr = ( uint32_t* ) persistPtr;
	*sizePtr = nextOutgoingSeqNum;
	++sizePtr;
	*sizePtr = maxI;
	++sizePtr;
	TradeState* tsPtr = ( TradeState* ) sizePtr;

	OSMI p = tradeStateMap.begin();
	for(; p != tradeStateMap.end() ; ++p) {
		p->second.lock(); 
		*tsPtr = p->second.tradeState;
		++tsPtr;
		p->second.unlock(); 
	}
	msync(persistPtr,persist_size,MS_SYNC | MS_INVALIDATE);
}

bool FIX_Interface::readEasyToBorrow(const string& _easy) {
	ifstream rfIn(_easy.c_str());

	if(rfIn.fail() ) {
		cout << "Unable to easy to borrow from file " << _easy << endl;
		return false;
	}
	std::string line;
	std::vector<std::string> tokens;
	long linecount = 0;
	while( ! rfIn.fail() ) {
		getline(rfIn,line);
		if( rfIn.fail() ) break;

		tokens.clear();
		split(line,tokens,"\t ");
		std::string symbol = tokens[0];
		OSMI p;
		if( findTradeContext(symbol,p) ) {
			p->second.lock();
			p->second.tradeState.easyToBorrow = true;
			p->second.unlock();
			++linecount;
		}
	}
	rfIn.close();

	return true;
}

void FIX_Interface::clearRejects() {
	OSMI p = tradeStateMap.begin();
	for(; p != tradeStateMap.end() ; ++p) {
		p->second.lock(); 
		p->second.tradeState.rejected = false;
		p->second.unlock(); 
	}
}

bool FIX_Interface::readRates(const string& _rates) {
	ifstream rfIn(_rates.c_str());

	if(rfIn.fail() ) {
		cout << "Unable to read rates from file " << _rates << endl;
		return false;
	}
	std::string line;
	std::vector<std::string> tokens;
	long linecount = 0;
	while( ! rfIn.fail() ) {
		getline(rfIn,line);
		if( rfIn.fail() ) return false;

		tokens.clear();
		split(line,tokens,"\t ");
		if( tokens.size() < 2 ) return false;
		if( tokens[0] == "SECFEERATE" ) {
			secFeeRate = atof(tokens[1].c_str());
			char buf[64];
			sprintf(buf,"SECFEERATE:\t%f\n",
					secFeeRate);
			fixEngineLogPtr->addMessageTokens(buf);
			continue;
		}
		if( tokens[0] == "EXECUTIONFEERATE" ) {
			executionFeeRate = atof(tokens[1].c_str());
			char buf[64];
			sprintf(buf,"EXECUTIONFEERATE:\t%f\n",
					executionFeeRate);
			fixEngineLogPtr->addMessageTokens(buf);
			continue;
		}
		if( tokens.size() == 3 ) {
			std::string exchange = tokens[0];
			double provideRate = atof(tokens[1].c_str());
			double takeRate = atof(tokens[2].c_str());
			takeMap[exchange] = takeRate;
			provideMap[exchange] = provideRate;
			char buf[64];
			sprintf(buf,"RATES:\t%s\tprovide=%f\ttake=%f\n",
					exchange.c_str(),
					provideRate,
					takeRate);
			fixEngineLogPtr->addMessageTokens(buf);
			continue;
		}
		fixEngineLogPtr->addMessageTokens("RATES: BAD_LINE: ",line.c_str(),"\n");
	
	}
	rfIn.close();

	return true;
}

inline bool FIX_Interface::removeStrandedOrder(OSMI _context,timespec& _time) {
	OVI p;
	bool found = locateOrderByTime(_context->second.orderVector,_time,p);
	char buf[256];
	sprintf(buf,"REMOVE_STRANDED %s %d %d found=%d\n",_context->second.tradeState.symbol,
		_time.tv_sec,
		_time.tv_nsec,
		found);
		fixEngineLogPtr->addMessageTokens(buf);

	if( found && (!p->confirmed) ) {
		_context->second.orderVector.erase(p); 
		return true;
	}
	return false;
}

inline bool FIX_Interface::locateOrderByTime(vector<Order>& _orderVectorRef,timespec& _time,OVI& p) {
	p = _orderVectorRef.begin();
	for(; p !=  _orderVectorRef.end(); ++p ) {
		if( (p->transactTime.tv_sec == _time.tv_sec) &&
			(p->transactTime.tv_nsec == _time.tv_nsec )) return true;
	}
	return false;
}
