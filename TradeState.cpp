#include "TradeState.h"

TradeState::TradeState() : easyToBorrow(false),orderedBuyShares(0),orderedSellShares(0),executedBuyShares(0),executedSellShares(0),orderedBuyDollars(0.0),orderedSellDollars(0.0),executedBuyDollars(0.0),executedSellDollars(0.0),rebateDollars(0.0),executionFeeDollars(0.0),secFeeDollars(0.0), position(0),overnightPosition(0), buySharesOutstanding(0), sellSharesOutstanding(0), lastExecutionTime({0,0}), lastInSeqNo(0), rejected(false) {}

TradeState::~TradeState() {}
