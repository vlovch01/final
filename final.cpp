#include <stdlib.h>
#include <unistd.h>
#include <thread>
#include <iostream>
#include <sstream>
#include <iterator>
#include <fstream>
#include <algorithm>
#include <fcntl.h>
#include <string>
#include <cstring>
#include <sys/epoll.h>
#include <streambuf>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
//#include <net/inet.h>
#include <netinet/in.h>
#include <fcntl.h>

#include "threadsafe_queue.h"
#define MAX_EVENTS 32
#define GETOP "GET"

struct Message
{
	std::string buffer;
	int fd;
};

threadsafe_queue<Message> gQueue;

int set_nonblock(int fd)
{
int flags;
#if defined (O_NONBLOCK)
	if( -1 == (flags = fcntl(fd, F_GETFL, 0)))
    {
        flags = 0;
    }
    return fcntl( fd, F_SETFL, flags | O_NONBLOCK, 0 );
#else
   flags = 1;
   return ioctl(fd, FIOBIO, &flags);
#endif
}

bool parseBuffer(const std::string& buff, std::string& filePath)
{
    if( buff.empty() )
	{
		return false;
	}
    std::string op = buff.substr(0, strlen(GETOP));
	if( strcmp( op.c_str(), GETOP ) != 0 )
	{
		return false;
	}

    size_t posPathstart = buff.find('/', 0);
	size_t endPath      = buff.find(' ', posPathstart);
	size_t endQuestion  = buff.find('?', posPathstart);
	endPath = std::min( endPath, endQuestion );
	filePath.assign( buff.substr(posPathstart, endPath - posPathstart));
    
    //std::stringstream strstr(buff);

	//std::istream_iterator<std::string> it(strstr);
	//std::istream_iterator<std::string> end;

	//std::set<std::string> strSet(it, end);
    return true;
}

bool readFile(const std::string& fullPath, std::string& content)
{
	std::ifstream fin(fullPath);
	if( fin.is_open() )
	{
		std::string temp((std::istreambuf_iterator<char>(fin)), std::istreambuf_iterator<char>());
		std::swap(content, temp );
		fin.close();
		return true;
	}

	return false;
}

void sendResponse(int fd, const std::string& response )
{
    int status = send(fd, response.c_str(), response.length(), MSG_NOSIGNAL );
	if( status == -1 )
	{
		perror("Send error\n");
	}
	shutdown( fd, SHUT_RDWR );
	close( fd );
}
void workerThread(std::string& directory)
{
    while(1)
	{
		Message m;
		if( gQueue.try_pop(m) )
		{
		//	std::shared_ptr<Message> resMessage = gQueue.wait_and_pop();
		    std::string filePath;
			if( parseBuffer(m.buffer, filePath) )
			{
				std::string fullPath(directory);
				fullPath.append(filePath);
				std::string content;
				if( readFile(fullPath, content ) )
				{
					std::stringstream ss;

					ss << "HTTP/1.0 200 OK";
					ss << "\r\n";
					ss << "Content-length: ";
					ss << content.size();
					ss << "\r\n";
					ss << "Content-Type: text/html";
					ss << "\r\n\r\n";
					ss << content;

				    sendResponse(m.fd, ss.str() );
				}
				else
				{
					//404
					std::stringstream ss;
					ss << "HTTP/1.0 404 NOT FOUND";
					ss << "\r\n";
					ss << "Content-length: ";
					ss << 0;
					ss << "\r\n";
					ss << "Content-Type: text/html";
					ss << "\r\n\r\n";
					sendResponse( m.fd, ss.str());
				}
			}
			else
			{
				//404
			}
		}
	}
}

int main(int argc, char** argv)
{

        if( argc < 6 )
        {
                std::cout << "Invalid number of argc " << std::endl;
                exit(1);
        }

        std::string strIp;
        std::string strPort;
        std::string directory;
        int c = 0;

        while( (c = getopt( argc, argv, "h:p:d:" )) != -1 )
        {
                switch( c )
                {
                        case 'h': strIp.append(optarg); break;

                        case 'p': strPort.append(optarg); break;

                        case 'd': directory.append(optarg); break;

                       default:
                                  std::cout << "invalid option " << std::endl;
                                  exit(1);
                }
        }

        //std::cout << "ip = " << strIp << " port = " << strPort << " directory = " << directory << std::endl;

        int MasterSocket = 0;
        if( (MasterSocket = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP ) ) < 0 ) 
		{
            perror("Error on creating socket\n");
            exit(1);
		}
		
		set_nonblock(MasterSocket);

        int optval = 1;
       	if(	setsockopt(MasterSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 )
        {
            perror("Error on seting sockoption\n");
            exit(1);
        }
     
       struct sockaddr_in sockAddr;
	   std::memset((void*) &sockAddr, 0, sizeof(sockAddr)); 
		
	   sockAddr.sin_family = AF_INET;
       sockAddr.sin_port   = htons(std::stoi(strPort));
       sockAddr.sin_addr.s_addr = htonl(INADDR_ANY);	   
       
	   if( bind(MasterSocket, (struct sockaddr*)&sockAddr, sizeof(sockAddr)) == -1 && 
			   errno != EADDRINUSE )
	   {
		   perror("Bind failed\n");
		   exit(1);
	   }

	   if( listen(MasterSocket, SOMAXCONN) == -1 )
	   {
		   perror("Error on listen\n");
		   exit(1);
	   }

	   int EPoll = epoll_create1(0);
	   if( EPoll == -1 )
	   {
		   perror("EPoll create error\n");
		   exit(1);
	   }
       
	   struct epoll_event event;
	   event.data.fd = MasterSocket;
	   event.events  = EPOLLIN;
       
	   int status = epoll_ctl( EPoll, EPOLL_CTL_ADD, MasterSocket, &event );
	   if( status == -1 )
	   {
		   perror("epoll_ctl\n");
		   exit(1); 
	   }

	   pid_t pid = fork();
	   if( pid < 0 ) 
	   {
		   perror("Fork error\n");
		   exit(1);
	   }

	   if( pid > 0 ) // parent
	   {
		   exit(0);
	   }
	   //child continues
	   umask(0);

	   std::thread worker(workerThread, std::ref(directory));

	   while(true) 
	   {
		   struct epoll_event Events[MAX_EVENTS];
		   int nConn = epoll_wait(EPoll, Events, MAX_EVENTS, -1);
		   if( nConn == -1 )
		   {
			   perror("epoll_wait\n");
			   exit(1);
		   }
           
		   for( int i = 0; i < nConn; ++i )
		   {
			   if( Events[i].data.fd == MasterSocket )
			   {
				   int SlaveSocket = accept( MasterSocket, 0, 0 );
				   if( SlaveSocket == -1 )
				   {
					   perror("Accepting error\n");
					   continue;
				   }
				   set_nonblock(SlaveSocket);

				   event.data.fd = SlaveSocket;
				   event.events  = EPOLLIN;
				   status = epoll_ctl( EPoll, EPOLL_CTL_ADD, SlaveSocket, &event );
                   if( status == -1 )
				   {
					   perror("Error on adding SlaveSocket\n");
					   continue;
				   }

			   }
			   else if( ( Events[i].events & EPOLLERR ) || ( Events[i].events & EPOLLHUP ) ||
					   !( Events[i].events & EPOLLIN ) )
			   {
				   perror("error occured with this descriptor\n");
				   shutdown(Events[i].data.fd, SHUT_RDWR );
				   close( Events[i].data.fd );
                   continue;
			   } 
			   else
			   {
                   //while(1)
				   {
					   char buffer[2048];
					   int rSize = recv( Events[i].data.fd, buffer, sizeof(buffer), MSG_NOSIGNAL );
					   if( rSize == 0 && errno != EAGAIN )
					   {
						   //shutdown( Events[i].data.fd, SHUT_RDWR );
						   //close( Events[i].data.fd );
					   } 
					   else 
					   {
                           if( rSize > 0 ) 
						   {
							   send( Events[i].data.fd, buffer, rSize, MSG_NOSIGNAL );
							   Message m;
							   m.buffer.assign(buffer);
							   m.fd = Events[i].data.fd;
							   gQueue.push(m);
							   //std::cout << "Recived: " << buffer << std::endl;
							   //remove socket from epoll
							   status = epoll_ctl( EPoll, EPOLL_CTL_DEL, Events[i].data.fd, &event);
							   if( status == -1 )
							   {
								   perror("epoll_ctl EPOLL_CTL_DEL\n");
								   continue;
							   }
						   }
					   }
				   }
			   }
		   }

//		   worker.join();
	   }
       return 0;
}

