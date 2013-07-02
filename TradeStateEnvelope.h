#ifndef __TRADESTATEENVELOPE__H
#define __TRADESTATEENVELOPE__H

#include <pthread.h>

#include <vector>

#include "TradeState.h"

class TradeStateEnvelope {
public:
	TradeStateEnvelope();
	void lock();
	void unlock();
	void getTradeState(TradeState& _tradeState,vector<Order>& _orderVectorRef);
	~TradeStateEnvelope();

	std::vector<Order> orderVector;
	TradeState tradeState;
	pthread_mutex_t mutex;
};

#endif // __TRADESTATEENVELOPE__H

