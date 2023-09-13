#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>

/*
	线程池类中有线程池数组，存储线程池数量，首先创建线程池启动线程，
	启动线程后，通过submitTask向线程池中添加任务，此时需要进行线程通信
	该线程同步，需要保证threadFunc取任务时，任务队列中有任务，Submit时
	需要保证任务队列还没有满。
	为了取到线程的返回值，由于该返回值由用户决定所以需要实现一个Any类来接收
	任意类型的数据，submit之后返回一个有Any数据的Result类，同时，Result类的
	对象创建在提交任务时，若创建成功返回数据类型，否则返回空
*/

//Task类型的前置声明
class Task;
//接收所有类型数据的类型
class Any {
public:
	Any() = default;
	//接收数据0
	template<typename T>
	Any(T data):base_(std::make_unique<Derive<T>>(data)){}

	//获取Derive中的数据
	template<typename T>
	T cast_() {
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) {
			throw "type is unmatch!";
		}
		return pd->data_;
	}

private:
	class Base {
	public:
		virtual ~Base() = default;
	};
	template<typename T>
	class Derive:public Base{
	public:
		Derive(T a) :data_(a) {};
		T data_;
	};
private:
	std::unique_ptr<Base> base_;
};

//实现一个信号量
class Semaphore {
public:
	Semaphore(int limit = 0) :resLimit_(limit) {};
	~Semaphore() {};

	//信号量的P操作
	void wait() {
		std::unique_lock<std::mutex> lock(mtx_);
		//while (resLimit_ == 0) {
		//	cond_.wait(lock);
		//}

		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	void post() {
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}

private:
	//信号量资源数
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;

};

//实现接收提交到任务队列的任务，执行完后的返回值
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	
	//setVal 的方法，获取任务返回值
	void setVal(Any any);
	//提供一个get方法，用户调用获取task的返回值
	Any get();


private:
	//存储任务的返回值
	Any res_;
	Semaphore sem_;
	//存放task类型
	std::shared_ptr<Task> task_;//指向对应获取返回值的任务对象
	std::atomic_bool isValid_;  //返回值是否有效
};

//线程池模式
enum class PoolMode {
	Mode_FIXED,  //固定线程数量
	Mode_CACHED,  //线程数量可动态增长
};

//线程类型
class Thread {
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();//线程析构
	//线程启动函数
	void start();

	//获取线程id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId; //保存线程id
};
//任务抽象基类
class Task {
public:
	Task();
	~Task() =default;
	void exec();
	//传入对应的Result对象
	void setResult(Result* res);
	
	virtual Any run() = 0;
public:
	//result生命周期比Task长，裸指针即可
	Result* result_;
};

/*
* 
	example:
	//task对象
	class Task1:public Task {
	public:
		virtual void run() {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			std::cout << std::this_thread::get_id() << std::endl;
		}
	};

	ThreadPool tpool;
	tpool.start(4);

	tpool.submitTask(std::shared_ptr<Task>(new Task1));
	tpool.submitTask(std::shared_ptr<Task>(new Task2(10,20)));

*/
//线程池类型
class ThreadPool {
public:
	ThreadPool();
	~ThreadPool();

	//设置线程池的工作模式
	void setMode(PoolMode mode);

	//开启线程池
	void start(int size);

	//线程池阈值设置
	void setTaskQueThreshHold(int threadhold);
	void setThreadSizeThreshHold(int threadhold);


	//给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	


private:
	//线程函数 从队列中消费任务
	void threadFunc(int threadId);

	//检查线程池的运行状态
	bool checkRunning()const;
private:
	//线程列表
	//std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;
	
	//初始线程个数
	size_t initThreadSize_;
	//记录线程池中总线程数量
	std::atomic_int curThreadNum_;
	//记录空闲线程的数量
	std::atomic_int freeThread;
	//初始化任务队列
	std::queue<std::shared_ptr<Task>> taskQue_;
	//任务个数
	std::atomic_int taskSize_;
	//任务数量阈值,上限
	int taskSizeThreshHold_;
	//线程数量上限
	int threadSizeThreshHold_;

	std::mutex taskQueMtx_;//保证任务队列线程安全
	std::condition_variable notFull_;//保证任务队列不满
	std::condition_variable notEmpty_;//保证任务队列不空
	std::condition_variable exit_;


	PoolMode poolMode_;//工作模式

	std::atomic_bool isStart;//表示当前线程池的启动状态


};

#endif
