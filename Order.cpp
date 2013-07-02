#include "Order.h"

Order::Order() : clOrdID(0),side(OS_UNKNOWN),ordType(OT_UNKNOWN),TIF(TIF_UNKNOWN),size(0),price(0.0),
		transactTime({0,0}),ackTime({0,0}),cancelTime({0,0}),
		executedShares(0),takeLiquidity(true),confirmed(false),replaced(false) {
}

void Order::display(char* _buffer) {
	sprintf(_buffer,"ORDER_ID=%u symbol=%s exch=%s side=%s size=%d price=%f executedShares=%d confirmed=%d\n",clOrdID,symbol.c_str(),exchange.c_str(), side2string(side).c_str(),size,price,executedShares,confirmed);
}

void Order::display(FILE* _stream) {
	char buffer[1024];
	display(buffer);
	fputs(buffer,_stream);
}

void Order::display(AsyncLogger* logger) {
	char buffer[4096];
	display(buffer);
	(*logger) << buffer;
}

void Order::display(AsyncLogger* logger,const char* _label) {
	char buffer[4096];
	display(buffer);
	logger->addMessageTokens(_label, buffer);
}

OrderSide string2Side(std::string _sideString) {
	if( _sideString == "B" ) return OS_BUY; 
	if( _sideString == "S" ) return OS_SELL; 
	if( _sideString == "SS" ) return OS_SELLSHORT; 
	return OS_UNKNOWN;
}

std::string side2string(OrderSide _side) {
	if( _side == OS_BUY ) return "B";
	if( _side == OS_SELL ) return "S";
	if( _side == OS_SELLSHORT ) return "SS";
	return "?";
}

OrderType string2Type(std::string _typeString) {
	if( _typeString == "L" ) return OT_LIMIT_ORDER; 
	if( _typeString == "M" ) return OT_MARKET_ORDER; 
	return OT_UNKNOWN;
}

OrderTIF string2TIF(std::string _TIFString) {
	if( _TIFString == "DAY" ) return TIF_DAY; 
	if( _TIFString == "IOC" ) return TIF_IOC; 
	return TIF_UNKNOWN;
}

Order::~Order() {}
