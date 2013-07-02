#ifndef __HFE_ASYNCLOGGER_H__
#define __HFE_ASYNCLOGGER_H__

#include <iostream>
#include <fstream>
#include <vector>
#include <pthread.h>


class AsyncLogger {
	public:
		AsyncLogger(const std::string&,bool _append=false);
		~AsyncLogger();
		void _write();
		void addMessageTokens(const std::string&);
		void addMessageTokens(const std::string& line,const std::string& line2);
		void addMessageTokens(const std::string& line,const std::string& line2,const std::string& line3);

	private:
		pthread_mutex_t _mutex;
		std::ofstream _log;
		std::vector<std::string> msg_q1, msg_q2;
		std::vector<std::string>* msg_q;

		void addMessageTokens(std::vector<std::string>&);

		friend AsyncLogger& operator<<(AsyncLogger&, const long&);
		friend AsyncLogger& operator<<(AsyncLogger&, const int&);
		friend AsyncLogger& operator<<(AsyncLogger&, const double&);
		friend AsyncLogger& operator<<(AsyncLogger&, const float&);
		friend AsyncLogger& operator<<(AsyncLogger&, const std::string&);
};

AsyncLogger& operator<<(AsyncLogger&, const long&);
AsyncLogger& operator<<(AsyncLogger&, const int&);
AsyncLogger& operator<<(AsyncLogger&, const double&);
AsyncLogger& operator<<(AsyncLogger&, const float&);
AsyncLogger& operator<<(AsyncLogger&, const std::string&);
#endif
