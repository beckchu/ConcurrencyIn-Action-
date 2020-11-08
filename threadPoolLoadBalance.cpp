#include<memory>
#include<atomic>
#include<mutex>
#include<queue>
#include<condition_variable>
#include<thread>
#include<vector>
#include<future>
/*
可返回执行结果的线程池
*/



//std::package_task是只移类型，function_wrapper使其具备copy语义，配合future实现thread完成通知
class function_wrapper {
	struct impl_base {
		virtual void call() = 0;
		virtual ~impl_base(){}
	};

	std::unique_ptr<impl_base> impl;//这里是类型擦除的关键，将具体类型转变为指针，类似std::any
	
	template<typename F>
	struct imp_type :impl_base {
		F f;
		imp_type(F&& f_):f(std::move(f_)){}
		void call() { f(); }
	};

public:
	template<typename F>//函数对象也不应该有参数
	function_wrapper(F&& f) :impl(new imp_type<F>(std::move(f))){}
	//这里的实现调用是没有参数的
	void operator()(){
		impl->call();
	}

	function_wrapper() = default;
	function_wrapper(function_wrapper&& other):impl(std::move(other.impl)){}
	function_wrapper& operator=(function_wrapper&& other) {
		impl = std::move(other.impl);
		return *this;
	}

	function_wrapper(const function_wrapper&) = delete;
	function_wrapper(function_wrapper&) = delete;
	function_wrapper& operator=(const function_wrapper&) = delete;
};

template<typename T>
class thread_safe_queue
{
private:
	mutable std::mutex mut;
	std::queue<T> data_queue;
	std::condition_variable data_cond;
public:
	thread_safe_queue() { }
	void push(T new_value) {
		std::lock_guard<std::mutex> lk(mut);
		data_queue.push(std::move(data));
		data_cond.notify_one();
	}
	void wait_and_pop(T& value) {
		std::unique_lock<std::mutex> lk(mut);
		data_cond.wait(lk, [this] {return !data_queue.empty(); });
		value = std::move(data_queue.front());
		data_queue.pop();
	}
	bool empty() const {
		std::lock_guard<std::mutex> lk(mut);
		return data_queue.empty();
	}
	std::shared_ptr<T> wait_and_pop() {
		std::unique_lock<std::mutex> lk(mut);
		data_cond.wait(lk, [this] {return !data_queue.empty(); });
		std::shared_ptr<T> res(
			std::make_shared<T>(std::move(data_queue.front())));
		data_queue.pop();
		return res;
	}
	bool try_pop(T& value) {
		std::lock_guard<std::mutex> lk(mut);
		if (data_queue.empty())
			return false;
		value = std::move(data_queue.front());
		data_queue.pop();
		return true;
	}
	std::shared_ptr<T> try_pop() {
		std::lock_guard<std::mutex> lk(mut);
		if (data_queue.empty())
			return std::shared_ptr<T>();
		std::shared_ptr<T> res(
			std::make_shared<T>(std::move(data_queue.front())));
		data_queue.pop();
		return res;
	}
};


class join_threads
{
	std::vector<std::thread>& threads;
public:
	explicit join_threads(std::vector<std::thread>& threads_) :
		threads(threads_) {}
	~join_threads()
	{
		for (unsigned long i = 0; i < threads.size(); ++i)
		{
			if (threads[i].joinable())
				threads[i].join();
		}
	}
};


class thread_pool {
private:
	std::atomic<bool> done;
	thread_safe_queue<function_wrapper> work_queue;
	std::vector<std::thread> threads;
	join_threads joiner;

public:
	
	thread_pool() : done(false), joiner(threads)
	{
		unsigned const thread_count = std::thread::hardware_concurrency();
		try
		{
			for (unsigned i = 0; i < thread_count; ++i)
			{
				threads.push_back(std::thread(&thread_pool::worker_thread, this));
			}
		}
		catch (...) {
			done = true;
			throw;
		}
	}

	void worker_thread() {
		while (!done) {
			function_wrapper task;
			if (work_queue.try_pop(task)) {
				task();//fuction_wrapper是函数对象
			}
			else {
				std::this_thread::yield();//reschedule this_thread仅仅是namespace
			}
		}
	}

	//submit后返回future对象，实现异步通知
	template<typename Function_Type>
	std::future<typename std::result_of<Function_Type()>::type> 
	submit(Function_Type f) {
		typedef typename std::result_of<Function_Type()>::type result_type;//直接看cplusplus，指的是函数返回类型
		std::packaged_task<result_type()> task(std::move(f));
		std::future<result_type> res(task.get_future());
		work_queue_push(std::move(task));
		return res;
	}

public:
	~thread_pool() {
		done = true;
	}
};
