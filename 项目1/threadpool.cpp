#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <mutex>
#include <chrono>

const int TaskMaxThreadHold = 1024;
const int ThreadSizeThreshHold_ = 100;
const int Thread_MAX_IDLE_TIME = 60;

//�̳߳ع��캯��
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
//�淶�����ֹ��죬������������д��
//��������߳�
ThreadPool::~ThreadPool() {
	isStart = false;
}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunning())return;
	poolMode_ = mode;
}
//����̳߳ص�����״̬
bool ThreadPool::checkRunning()const {
	return isStart;
}

//�̳߳���ֵ����
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
//�����̳߳�
void ThreadPool::start(int size = 4)
{
	isStart = true;
	//��¼�̳߳�ʼ����
	initThreadSize_ = size;
	curThreadNum_ = size;
	//�����̶߳���
	for (size_t i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int thread_id = ptr->getId();
		threads_.emplace(thread_id,std::move(ptr));
		freeThread++;
	}
	//�����߳�
	for (size_t i = 0; i < initThreadSize_; i++) {
		//std::cout << "�߳�����" << std::endl;
		threads_[i]->start();
		//�ȴ��̳߳��������е��̷߳���  ����//����ִ������
	}
}

//���̳߳��ύ����  ����������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//��ȡ��
	std::unique_lock<std::mutex> loc(taskQueMtx_);
	
	if(!(notFull_.wait_for(loc, std::chrono::seconds(1), 
		[&]()->bool {return taskQue_.size() < (size_t)taskSizeThreshHold_; })))
	{
		//�ȴ�һ�룬���������Ȼ������
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp,false); //return ����һ��Result����
	}
	//����п��࣬������ŵ����������
	taskQue_.emplace(sp);
	taskSize_++;
	//���񲻿�����notEmptyqȥ����֪ͨ
	notEmpty_.notify_all();

	//�������������Ϳ����߳��������ж��Ƿ���Ҫ�����µ��߳�
	if (poolMode_ == PoolMode::Mode_CACHED
		&& taskSize_>freeThread
		&& curThreadNum_ < threadSizeThreshHold_) 
	{
		//�������߳�
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

//�������������������
void ThreadPool::threadFunc(int threadId) {
	auto lastTime = std::chrono::high_resolution_clock().now();
	for (;;) {
		std::shared_ptr<Task> t;
		{//��ȡ���������������
			std::unique_lock<std::mutex> loc(taskQueMtx_);
			std::cout << std::this_thread::get_id() << "���Ի�ȡ����" << std::endl;
			//���û������������ȴ�.Cache�����̵߳Ļ���
			//��ǰִ��ʱ��-��һ���߳�ִ��ʱ�䳬��60����߳̽��л���
			if (poolMode_ == PoolMode::Mode_CACHED) {
				//ÿһ���з���һ�Σ�������ֳ�ʱ���أ������������ִ�з���
				
				while (taskQue_.size()==0) {
					
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(loc, std::chrono::seconds(1))) {
						
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						/*std::cout << dur << "lock_here?" << std::endl;*/
						if (dur.count() >= Thread_MAX_IDLE_TIME && curThreadNum_ > initThreadSize_) {
							//�����߳�
							//��¼������ֵ�޸�
							curThreadNum_--;
							freeThread--;
							//ͨ���߳�id==���̶߳���==��ɾ���߳�
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
			//ȡ����
			t = taskQue_.front();
			taskQue_.pop();
			taskSize_--;
			
			std::cout << std::this_thread::get_id() << "��ȡ����ɹ�" << std::endl;
			//��������л������񣬼���֪ͨ�����߳�ִ��
			if (taskSize_ > 0) {
				notEmpty_.notify_all();
			}

			//����ȡ��
			notFull_.notify_all();
		}//task->run ʱ����Ҫ��ȡ��
		if (t != nullptr) {
			//ִ������
			t->exec();
		}
		//�߳�ִ�����ʱ��
		lastTime = std::chrono::high_resolution_clock().now();
		freeThread++;
	}
}
 
///////////////  Thread

//���캯����ʵ��
Thread::Thread(ThreadFunc func) :
	func_(func),
	threadId(generateId_++)
{

}

int Thread::generateId_ = 0;

//��������
Thread::~Thread() {}

int Thread::getId() const {
	return threadId;
}

//�̷߳���ʵ��
void Thread::start() {
	//����һ���߳�  ִ���̺߳���
	std::thread t1(func_,threadId);//cpp11 ��thread_���������������start()������

	//�̷߳���
	t1.detach();
}

////////////////// TASK����ʵ��
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
/////////////////// Result����ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid):
	task_(task), isValid_(isValid){
	task_->setResult(this);
}

void Result::setVal(Any any) {
	std::cout << "setVal" << std::endl;
	// �洢task�ķ���ֵ
	this->res_ = std::move(any);
	//�Ѿ���ȡ������ķ���ֵ
	sem_.post();
}

Any Result::get () {
	if (!isValid_) {
		return "";
	}
	sem_.wait(); //task����û��ִ���������
	//any�еĳ�Ա������unique_ptr
	return std::move(res_);
}

