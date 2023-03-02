
DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
else
	CXXFLAGS += -O2

endif

CXX = g++
TARGET = webserver
OBJ = main.cpp  ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp  webserver.cpp config.cpp

$(TARGET): $(OBJ)
	$(CXX) -o $@ $^ $(CXXFLAGS) -lpthread -lmysqlclient


.PHONY: clean
clean:
	rm -rf $(TARGET)


#version 1
# TARGET = server
# OBJ = main.o sql_connection_pool.o http_conn.o log.o lst_timer.o webserver.o config.o 

# $(TARGET): $(OBJ)
# 	$(CXX) -o $@ $^ $(CXXFLAGS) -lpthread -lmysqlclient

# sql_connection_pool.o: ./CGImysql/sql_connection_pool.cpp
# 	$(CXX) -c ./CGImysql/sql_connection_pool.cpp

# http_conn.o: ./http/http_conn.cpp
# 	$(CXX) -c ./http/http_conn.cpp

# log.o: ./log/log.cpp
# 	$(CXX) -c ./log/log.cpp

# lst_timer.o: ./timer/lst_timer.cpp
# 	$(CXX) -c ./timer/lst_timer.cpp

# webserver.o: webserver.cpp
# 	$(CXX) -c webserver.cpp

# config.o: config.cpp
# 	$(CXX) -c config.cpp

# main.o: main.cpp
# 	$(CXX) -c main.cpp


# #version 2
# TARGET = server
# SRC = $(wildcard *.cpp)
# # 这里不能直接列出 *.o，因为还没生成
# OBJ = $(patsubst %.cpp, %.o, $(SRC))

# $(TARGET): $(OBJ)
# 	$(CXX) -o $@ $^


# .PHONY: clean
# clean:
# 	rm -rf server