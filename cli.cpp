#include <cstdio>

#include <string>
#include <vector>
#include <map>


#include "split.h"
#include "FIX_Interface.h"

struct OrderStruct {
	std::string type;
	std::string side;
	long shares;
	double price;
	bool confirmed;
	long filledShares;
};

void chomp(std::string& _s) {
	if( ! _s.empty() && _s[_s.length()-1] == '\n' ) _s.erase(_s.length()-1);
	return;
}

uint32_t midnightSeconds = 0;

int main(void) {

	map<std::string,OrderStruct> orderMap;

        FIX_Interface fi("99.999.99.99",60000);
        
        fi.senderCompID = "TEST";
        fi.targetCompID = "TEST";
        fi.account = "outuat";

	bool keepGoing = true;

	while(keepGoing) {
		std::cout << ">";
		char command[256];

		fgets(command, 255, stdin);

		vector<std::string> tokens;
		split(command, tokens, " ");

		size_t numTokens= tokens.size();

		if( numTokens < 1 ) {
			std::cout << "INVALID COMMAND" << std::endl;
			continue;
		}

		chomp(tokens[numTokens-1]);		


		if( tokens[0] == "q" ) {
			keepGoing = false;
			std::cout << "Quitting..." << std::endl;
			continue;
		}

		if( tokens[0] == "c" ) {
			if( tokens.size() < 2 ) {
				std::cout << "CANCEL Usage: c <order_id>" << std::endl;
				continue;
			}
			uint32_t oid = atoi(tokens[1].c_str());
			if( oid < 1 ) {
				std::cout << "Invalid order id" << std::endl;
				continue;
			}
			fi.cancelOrder(oid);
			continue;
		}

		if( tokens[0] == "cr" ) {
			if( tokens.size() < 4 ) {
				std::cout << "CANCEL_REPLACE_PRICE Usage: crp <order_id> <new_price> <new_size>" << std::endl;
				continue;
			}
			uint32_t oid = atoi(tokens[1].c_str());
			if( oid < 1 ) {
				std::cout << "Invalid order id" << std::endl;
				continue;
			}

			double price = atof(tokens[2].c_str());
			long size = atoi(tokens[3].c_str());
			fi.cancelReplaceOrder(oid,price,size);

			continue;
		}

		if( tokens[0] == "l" ) {
			//list orders
			std::cout << "LIST OF ORDERS..." << std::endl;
			fi.listOrders();
			continue;
		}

		if( tokens[0] == "ct" ) {
			std::cout << "CONNECTING..." << std::endl;
			if(!fi.initConnection()) {
				std::cout << "Unable to connect!" << std::endl;
				continue;
			}
			if(!fi.logon()) {
				std::cout << "Unable to logon!" << std::endl;
				continue;
			}

			continue;
		}

		if( tokens[0] == "ca" ) {
			fi.massCancel();
			continue;
		}

		if( tokens[0] == "n" ) {
			if( tokens.size() < 7 ) {
				std::cout << "New order usage: n <type> <TIF> <buySell> <shares> <symbol> <exchange> [price]" << std::endl;
				continue;
			}


			double price(0.0);

			if( (tokens[1] != "M") && (tokens[1] != "MOO") && (tokens[1] != "MOC") ) {
				if( tokens.size() < 8 ) {
					std::cout << "Price required for non market orders." << std::endl;
					std::cout << "New order usage: n <type> <TIF> <buySell> <shares> <symbol> <exchange> [price]" << std::endl;
					continue;
				}

				price = atof(tokens[7].c_str());
			}

			Order order;
			order.ordType = string2Type(tokens[1]);
			order.TIF = string2TIF(tokens[2]);
			order.side = string2Side(tokens[3]);

			if( order.ordType == OT_UNKNOWN ) {
				std::cout << "Invalid order type" << std::endl;
				continue;
			}
			if( order.TIF == TIF_UNKNOWN ) {
				std::cout << "Invalid order TIF" << std::endl;
				continue;
			}

			if( order.side == OS_UNKNOWN ) {
				std::cout << "Invalid order Side" << std::endl;
				continue;
			}


			long shares(atoi(tokens[4].c_str()));
			std::string symbol = tokens[5];
			std::string exchange = tokens[6];

			order.symbol = symbol;
			order.size = shares;
			order.price = price;
			order.exchange = exchange;
			
			OSMI context = fi.getTradeContext(symbol);

			fi.sendOrder(order,context);

			continue;
		}

	}

}
