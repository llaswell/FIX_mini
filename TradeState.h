#ifndef _TRADESTATE__H
#define _TRADESTATE__H

#include <sys/time.h>
#include <cstdint>
#include <vector>


using namespace std;

class Order;

class TradeState {
public:
	TradeState();

	~TradeState();

	char symbol[10];
	bool easyToBorrow;


	uint32_t orderedBuyShares;
	uint32_t orderedSellShares;
	uint32_t executedBuyShares;
	uint32_t executedSellShares;

	double orderedBuyDollars;
	double orderedSellDollars;
	double executedBuyDollars;
	double executedSellDollars;

	double rebateDollars;
	double executionFeeDollars;
	double secFeeDollars;

	long position;
	long overnightPosition;

	uint32_t buySharesOutstanding;
	uint32_t sellSharesOutstanding;

	struct timespec lastExecutionTime;

	uint32_t lastInSeqNo;

	bool rejected;
}
;

#include "Order.h"

#endif //_TRADESTATE__H
