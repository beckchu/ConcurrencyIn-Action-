#include<atomic>
#include<memory>
#include<queue>
#include<future>
#include<mutex>

//std::package_task是只移类型，function_wrapper使其具备copy语义，配合future实现thread完成通知
class function_wrapper {
	struct impl_base {
		virtual void call() = 0;
		virtual ~impl_base() {}
	};

	std::unique_ptr<impl_base> impl;//这里是类型擦除的关键，将具体类型转变为指针，类似std::any

	template<typename F>
	struct imp_type :impl_base {
		F f;
		imp_type(F&& f_) :f(std::move(f_)) {}
		void call() { f(); }
	};

public:
	template<typename F>//函数对象也不应该有参数
	function_wrapper(F&& f) :impl(new imp_type<F>(std::move(f))) {}
	//这里的实现调用是没有参数的
	void operator()() {
		impl->call();
	}

	function_wrapper() = default;
	function_wrapper(function_wrapper&& other) :impl(std::move(other.impl)) {}
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

/////////////////////////////////////////////////////////////////////////////////////////
//thread_pool最大的问题就是解决queue中data的负载均衡问题

class work_stealing_queue
{
private:
	typedef function_wrapper data_type;
	std::deque<data_type> the_queue;
	mutable std::mutex the_mutex;
public:
	work_stealing_queue() {}
	work_stealing_queue(const work_stealing_queue& other) = delete;
	work_stealing_queue& operator=(
		const work_stealing_queue& other) = delete;
	void push(data_type data)
	{
		std::lock_guard<std::mutex> lock(the_mutex);
		the_queue.push_front(std::move(data));
	}
	bool empty() const
	{
		std::lock_guard<std::mutex> lock(the_mutex);
		return the_queue.empty();
	}

	bool try_pop(data_type& res)
	{
		std::lock_guard<std::mutex> lock(the_mutex);
		if (the_queue.empty())
		{
			return false;
		}
		res = std::move(the_queue.front());
		the_queue.pop_front();
		return true;
	}
	bool try_steal(data_type& res)//和try_pop的逻辑一样
	{
		std::lock_guard<std::mutex> lock(the_mutex);
		if (the_queue.empty())
		{
			return false;
		}
		res = std::move(the_queue.back());
		the_queue.pop_back();
		return true;
	}
};

class thread_pool
{
	typedef function_wrapper task_type;
	std::atomic_bool done;
	thread_safe_queue<task_type> pool_work_queue;
	std::vector<std::unique_ptr<work_stealing_queue> > queues;
	std::vector<std::thread> threads;
	join_threads joiner;
	static thread_local work_stealing_queue* local_work_queue;
	static thread_local unsigned my_index;

	void worker_thread(unsigned my_index_);

	bool pop_task_from_local_queue(task_type& task);
	bool pop_task_from_pool_queue(task_type& task);
	bool pop_task_from_other_thread_queue(task_type& task);
public:
	thread_pool();
	~thread_pool();
	void run_pending_task();

	template<typename FunctionType>
	std::future<typename std::result_of<FunctionType()>::type>
		submit(FunctionType f);
};

thread_pool::thread_pool() : done(false), joiner(threads)
{
	unsigned const thread_count = std::thread::hardware_concurrency();
	try {
		for (unsigned i = 0; i < thread_count; ++i)
		{
			queues.push_back(std::unique_ptr<work_stealing_queue>(//每个线程对应一个work_stealing_queue
				new work_stealing_queue));
			threads.push_back(std::thread(&thread_pool::worker_thread, this, i));
		}
	}
	catch (...) {
		done = true;
		throw;
	}
}

void thread_pool::worker_thread(unsigned my_index_)
{
	my_index = my_index_;
	local_work_queue = queues[my_index].get();//这里将local_work_queue和work_stealing_queue关联
	while (!done)
	{
		run_pending_task();
	}
}

void thread_pool::run_pending_task()
{
	task_type task;

	if (pop_task_from_local_queue(task) ||
		pop_task_from_pool_queue(task) ||
		pop_task_from_other_thread_queue(task))
	{
		task();
	}
	else
	{
		std::this_thread::yield();
	}
}

bool thread_pool::pop_task_from_local_queue(task_type& task)
{
	return local_work_queue && local_work_queue->try_pop(task);
}

bool thread_pool::pop_task_from_pool_queue(task_type& task)
{
	return pool_work_queue.try_pop(task);
}

bool thread_pool::pop_task_from_other_thread_queue(task_type& task)//从其他线程的local_thread获取task
{
	for (unsigned i = 0; i < queues.size(); ++i)
	{
		unsigned const index = (my_index + i + 1) % queues.size();
		if (queues[index]->try_steal(task))
		{
			return true;
		}
	}
	return false;
}

template<typename FunctionType>
std::future<typename std::result_of<FunctionType()>::type>
thread_pool::submit(FunctionType f)
{
	typedef typename std::result_of<FunctionType()>::type result_type;
	std::packaged_task<result_type()> task(f);
	std::future<result_type> res(task.get_future());
	if (local_work_queue)//程序正常的话都会有local_work_queue,   pool_work_queue感觉就没用了，应该设置一个上限，超过上限就从thread_pool中获取
	{
		local_work_queue->push(std::move(task));
	}
	else
	{
		pool_work_queue.push(std::move(task));
	}
	return res;
}

thread_pool:: ~thread_pool()
{
	done = true;
}

