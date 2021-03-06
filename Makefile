CXX = g++
#2、定义您自己的可执行文件名称
PROGRAM_1=media

##################################################################### #
#3、指定您必须生成的工程文件

SOURCE_1 = $(wildcard *.cpp) \
           $(wildcard tinyxml2/*.cpp)

OBJECTS_1 = $(SOURCE_1:.cpp=.o) 

CFLAGS = -g -fpermissive -std=c++11 -Iosip2/include -Ilibrtmp/include
LDFLAGS = -Losip2/lib -losipparser2 -Llibrtmp/lib -lrtmp -lpthread

.PHONY: all
all: $(PROGRAM_1)

clean:
	@echo "[Cleanning...]"
	@rm -f $(OBJECTS_1) $(PROGRAM_1) 

%.o: %.cpp
	$(CXX) $(CFLAGS) -o $@ -c $<


$(PROGRAM_1): $(OBJECTS_1)
	$(CXX) $(CFLAGS) -o $@ $^ $(LDFLAGS)
