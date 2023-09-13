#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <mutex>
#include <chrono>
const int TaskMaxThreadHold = 1024;

//线程池构造函数
ThreadPool::ThreadPool():
	initThreadSize_(4),
	taskSize_(0), 
	taskSizeThreshHold_(TaskMaxThreadHold),
	poolMode_(PoolMode::Mode_FIXED){
	
}
//规范：出现构造，析构函数必须写出
ThreadPool::~ThreadPool() {	}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}


//线程池阈值设置
void ThreadPool::setTaskQueThreshHold(int threadhold)
{
	taskSizeThreshHold_ = threadhold;
}

//给线程池提交任务  向任务队列添加任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex> loc(taskQueMtx_);
	//线程通信 等待
	//while (taskQue_.size() == taskSizeThreshHold_) {
	//	notFull_.wait(loc);
	//}
	//满足小于则继续进行，否则阻塞
	if(!(notFull_.wait_for(loc, std::chrono::seconds(1), 
		[&]()->bool {return taskQue_.size() < (size_t)taskSizeThreshHold_; })))
	{
		//等待一秒，任务队列依然是满的
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp,false); //return 的是一个Result对象
	}
	//如果有空余，把任务放到任务队列中国
	taskQue_.emplace(sp);
	taskSize_++;
	//任务不空则在notEmptyq去进行通知
	notEmpty_.notify_all();
	return Result(sp);
}

//开启线程池
void ThreadPool::start(int size = 4)
{
	//记录线程初始数量
	initThreadSize_ = size;
	//创建线程对象
	for (size_t i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		threads_.emplace_back(std::move(ptr));
	}
	//启动线程
	for (size_t i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
	}
}	

//从任务队列中消费任务
void ThreadPool::threadFunc() {
	for (;;) {
		std::shared_ptr<Task> t;
		{//获取锁，访问任务队列
			std::unique_lock<std::mutex> loc(taskQueMtx_);
			std::cout << std::this_thread::get_id() << "尝试获取任务" << std::endl;
			//如果没有任务来，则等待
			notEmpty_.wait(loc, [&]()->bool {return taskQue_.size() > 0; });

			//取任务
			t = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			std::cout << std::this_thread::get_id() << "获取任务成功" << std::endl;
			//若任务队列还有任务，继续通知其余线程执行
			if (taskSize_ > 0) {
				notEmpty_.notify_all();
			}

			//任务取出
			notFull_.notify_all();
		}//task->run 时不需要获取锁
		if (t != nullptr) {
			//执行任务
			t->exec();
		}
	}
}

///////////////

//构造函数的实现
Thread::Thread(ThreadFunc func):func_(func){

}

//析构函数
Thread::~Thread() {}

//线程方法实现
void Thread::start() {
	//创建一个线程  执行线程函数
	std::thread t1(func_);//cpp11 中thread_对象的生命周期在start()函数中

	//线程分离
	t1.detach();
}

////////////////// TASK方法实现
Task::Task() :result_(nullptr) {}

void Task::exec() {
	if (result_ != nullptr) {
		result_->setVal(run());
	}
}

void Task::setResult(Result* res) {
	std::cout << "setResult" << std::endl;
	result_ = res;
}
/////////////////// Result方法实现
Result::Result(std::shared_ptr<Task> task, bool isValid):
	task_(task), isValid_(isValid){
	task_->setResult(this);
}

void Result::setVal(Any any) {
	// 存储task的返回值
	this->res_ = std::move(any);
	//已经获取了任务的返回值
	sem_.post();
}

Any Result::get () {
	if (!isValid_) {
		return "";
	}
	sem_.wait(); //task任务没有执行完会阻塞
	//any中的成员变量是unique_ptr
	return std::move(res_);
}

