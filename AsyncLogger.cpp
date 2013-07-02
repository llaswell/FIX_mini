#include "AsyncLogger.h"

AsyncLogger::AsyncLogger(const std::string& log_path,bool _append) {
	if(_append)
		_log.open(log_path.c_str(),std::ios::app);
	else
		_log.open(log_path.c_str());
	if(_log.is_open())
		std::cout << "CREATED LOGGER " << log_path <<std::endl;
	pthread_mutex_init(&_mutex, NULL);
	msg_q = &msg_q1;
}

void AsyncLogger::_write() {
	std::vector<std::string>* read_q;
	pthread_mutex_lock(&_mutex);
	read_q = msg_q;
	msg_q = (&msg_q1==msg_q)?&msg_q2:&msg_q1;
	pthread_mutex_unlock(&_mutex);
	
	std::vector<std::string>::iterator it = read_q->begin();
	for(; it != read_q->end(); ++it) {
		_log << *it;
	}
	_log.flush();
	read_q->clear();
}

void AsyncLogger::addMessageTokens(std::vector<std::string>& tokens) {
	pthread_mutex_lock(&_mutex);
	for(int i=0; i<tokens.size(); ++i) {
		msg_q->push_back(tokens[i]);
	}
	pthread_mutex_unlock(&_mutex);
	tokens.clear();
}

void AsyncLogger::addMessageTokens(const std::string& line) {
	pthread_mutex_lock(&_mutex);
	msg_q->push_back(line);
	pthread_mutex_unlock(&_mutex);
}

void AsyncLogger::addMessageTokens(const std::string& line,const std::string& line2) {
	pthread_mutex_lock(&_mutex);
	msg_q->push_back(line);
	msg_q->push_back(line2);
	pthread_mutex_unlock(&_mutex);
}

void AsyncLogger::addMessageTokens(const std::string& line,const std::string& line2,const std::string& line3) {
	pthread_mutex_lock(&_mutex);
	msg_q->push_back(line);
	msg_q->push_back(line2);
	msg_q->push_back(line3);
	pthread_mutex_unlock(&_mutex);
}

AsyncLogger::~AsyncLogger() {
	pthread_mutex_destroy(&_mutex);
	_log.close();
}

AsyncLogger& operator<<(AsyncLogger& logger, const std::string& line) {
	logger.addMessageTokens(line);
	return logger;
}
AsyncLogger& operator<<(AsyncLogger& logger, const double& line) {
	char buf[1024];
	sprintf(buf, "%f", line);
	logger.addMessageTokens(std::string(buf));
	return logger;
}
AsyncLogger& operator<<(AsyncLogger& logger, const float& line) {
	char buf[1024];
	sprintf(buf, "%f", line);
	logger.addMessageTokens(std::string(buf));
	return logger;
}
AsyncLogger& operator<<(AsyncLogger& logger, const int& line) {
	char buf[1024];
	sprintf(buf, "%d", line);
	logger.addMessageTokens(std::string(buf));
	return logger;
}
AsyncLogger& operator<<(AsyncLogger& logger, const long& line) {
	char buf[1024];
	sprintf(buf, "%ld", line);
	logger.addMessageTokens(std::string(buf));
	return logger;
}
