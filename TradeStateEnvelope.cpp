#include "TradeStateEnvelope.h"

TradeStateEnvelope::TradeStateEnvelope() {
	pthread_mutex_init(&mutex,NULL);
}

TradeStateEnvelope::~TradeStateEnvelope() {}

void TradeStateEnvelope::lock() {
	pthread_mutex_lock(&mutex);
}

void TradeStateEnvelope::unlock() {
	pthread_mutex_unlock(&mutex);
}

void TradeStateEnvelope::getTradeState(TradeState& _tradeState,vector<Order>& _orderVectorRef) {
	lock();
	//tradeState.position = 7900;
	_tradeState = tradeState;	
	_orderVectorRef = orderVector;
	unlock();	
}
