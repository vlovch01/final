#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>

template <typename T>
class threadsafe_queue
{
	public:
       threadsafe_queue() {};

	   threadsafe_queue(const threadsafe_queue& ) = delete;
	   threadsafe_queue& operator=(const threadsafe_queue& ) = delete;

	   std::shared_ptr<T> wait_and_pop()
	   {
		   std::unique_lock<std::mutex> lk(m_mutex);
		   m_cv.wait(lk, [this]{return !m_queue.empty();});
		   std::shared_ptr<T> res = m_queue.front();
		   m_queue.pop();
		   return res;
	   }

	   void push(T val)
	   {
           std::shared_ptr<T> data(std::make_shared<T>(std::move(val)));
		   //std::lock_guard<std::mutex> lg(m_mutex);
		   m_queue.push(data);
		   m_cv.notify_one();   
	   }

	   bool try_pop(T& val)
	   {
		   std::lock_guard<std::mutex> lg(m_mutex);
		   if( m_queue.empty() )
		   {
			   return false;
		   }
		   val = std::move(*m_queue.front());
		   m_queue.pop();
		   return true;
	   }

	   bool empty() const
	   {
		   std::lock_guard<std::mutex> lg(m_mutex);
		   return m_queue.empty();
	   }
	private:
		std::queue<std::shared_ptr<T> > m_queue;
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
};
