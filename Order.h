#ifndef ORDER_H
#define ORDER_H

#include <string>
#include <cstdio>

#include "AsyncLogger.h"
#include "TradeState.h"

enum OrderSide { OS_UNKNOWN='?', OS_BUY='1', OS_SELL='2', OS_SELLSHORT='5' };
enum OrderType { OT_UNKNOWN='?', OT_LIMIT_ORDER='2' , OT_MARKET_ORDER='1' };
enum OrderTIF { TIF_UNKNOWN='?', TIF_DAY='0' , TIF_IOC='3' };

using namespace std;

std::string side2string(OrderSide _side);
OrderSide string2Side(std::string _sideString);
OrderType string2Type(std::string _typeString);
OrderTIF string2TIF(std::string _TIFString);

class Order {
public:
  Order();
  Order(struct order_params*);
  void display(char* _buffer);
  void display(FILE* _stream);
  void display(AsyncLogger*);
  void display(AsyncLogger*,const char* _label);
  virtual ~Order();

  string symbol;
  uint32_t clOrdID;
  OrderSide side;
  OrderType ordType;
  OrderTIF TIF;
  uint32_t size;
  std::string exchange;
  uint32_t executedShares;
  std::string exchangeCode;
  double price;
  timespec transactTime;
  timespec cancelTime;
  timespec ackTime;
  string sendTimeUTC;
  bool takeLiquidity;
  bool confirmed;
  bool replaced;

};

#endif //ORDER_H
