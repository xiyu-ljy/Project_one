#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <mutex>
#include <chrono>

const int TaskMaxThreadHold = 1024;
const int ThreadSizeThreshHold_ = 100;
const int Thread_MAX_IDLE_TIME = 60;

//线程池构造函数
ThreadPool::ThreadPool():
	initThreadSize_(0),
	taskSize_(0), 
	taskSizeThreshHold_(TaskMaxThreadHold),
	threadSizeThreshHold_(ThreadSizeThreshHold_),
	poolMode_(PoolMode::Mode_FIXED),
	isStart(false),
	curThreadNum_(0),
	freeThread(0){
	
}
//规范：出现构造，析构函数必须写出
//回收相关线程
ThreadPool::~ThreadPool() {
	isStart = false;
}

//设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunning())return;
	poolMode_ = mode;
}
//检查线程池的运行状态
bool ThreadPool::checkRunning()const {
	return isStart;
}

//线程池阈值设置
void ThreadPool::setTaskQueThreshHold(int threadhold)
{
	taskSizeThreshHold_ = threadhold;
}
void ThreadPool::setThreadSizeThreshHold(int threadhold) {
	if (checkRunning())return;
	if (poolMode_ == PoolMode::Mode_FIXED)
	{
		threadSizeThreshHold_ = threadhold;
	}
}
//开启线程池
void ThreadPool::start(int size = 4)
{
	isStart = true;
	//记录线程初始数量
	initThreadSize_ = size;
	curThreadNum_ = size;
	//创建线程对象
	for (size_t i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int thread_id = ptr->getId();
		threads_.emplace(thread_id,std::move(ptr));
		freeThread++;
	}
	//启动线程
	for (size_t i = 0; i < initThreadSize_; i++) {
		//std::cout << "线程启动" << std::endl;
		threads_[i]->start();
		//等待线程池里面所有的线程返回  阻塞//正在执行任务
	}
}

//给线程池提交任务  向任务队列添加任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//获取锁
	std::unique_lock<std::mutex> loc(taskQueMtx_);
	
	if(!(notFull_.wait_for(loc, std::chrono::seconds(1), 
		[&]()->bool {return taskQue_.size() < (size_t)taskSizeThreshHold_; })))
	{
		//等待一秒，任务队列依然是满的
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp,false); //return 的是一个Result对象
	}
	//如果有空余，把任务放到任务队列中
	taskQue_.emplace(sp);
	taskSize_++;
	//任务不空则在notEmptyq去进行通知
	notEmpty_.notify_all();

	//根据任务数量和空闲线程数量，判断是否需要创建新的线程
	if (poolMode_ == PoolMode::Mode_CACHED
		&& taskSize_>freeThread
		&& curThreadNum_ < threadSizeThreshHold_) 
	{
		//创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId,std::move(ptr));
		threads_[threadId]->start();
		curThreadNum_++;
		freeThread++;
		std::cout << "create new thread" << std::endl;
	}

	return Result(sp);
}

//从任务队列中消费任务
void ThreadPool::threadFunc(int threadId) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	for (;;) {
		std::shared_ptr<Task> t;
		{//获取锁，访问任务队列
			std::unique_lock<std::mutex> loc(taskQueMtx_);
			std::cout << std::this_thread::get_id() << "尝试获取任务" << std::endl;
			//如果没有任务来，则等待.Cache多余线程的回收
			//当前执行时间-上一次线程执行时间超过60秒对线程进行回收
			if (poolMode_ == PoolMode::Mode_CACHED) {
				//每一秒中返回一次，如何区分超时返回？还是有任务待执行返回
				
				while (taskQue_.size()==0) {
					
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(loc, std::chrono::seconds(1))) {
						
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						/*std::cout << dur << "lock_here?" << std::endl;*/
						if (dur.count() >= Thread_MAX_IDLE_TIME && curThreadNum_ > initThreadSize_) {
							//回收线程
							//记录数量的值修改
							curThreadNum_--;
							freeThread--;
							//通过线程id==》线程对象==》删除线程
							threads_.erase(threadId);

							std::cout << "delete thread" << std::endl;

							return;
						}
					}
				}
			}
			else {
				notEmpty_.wait(loc, [&]()->bool {return taskQue_.size() > 0; });
			}
			
			
			freeThread--;
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
		//线程执行完的时间
		lastTime = std::chrono::high_resolution_clock().now();
		freeThread++;
	}
}
 
///////////////  Thread

//构造函数的实现
Thread::Thread(ThreadFunc func) :
	func_(func),
	threadId(generateId_++)
{

}

int Thread::generateId_ = 0;

//析构函数
Thread::~Thread() {}

int Thread::getId() const {
	return threadId;
}

//线程方法实现
void Thread::start() {
	//创建一个线程  执行线程函数
	std::thread t1(func_,threadId);//cpp11 中thread_对象的生命周期在start()函数中

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
	/*std::cout << "setResult" << std::endl;*/
	result_ = res;
}
/////////////////// Result方法实现
Result::Result(std::shared_ptr<Task> task, bool isValid):
	task_(task), isValid_(isValid){
	task_->setResult(this);
}

void Result::setVal(Any any) {
	std::cout << "setVal" << std::endl;
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

