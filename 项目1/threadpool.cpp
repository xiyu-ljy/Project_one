#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <mutex>
#include <chrono>
const int TaskMaxThreadHold = 1024;

//�̳߳ع��캯��
ThreadPool::ThreadPool():
	initThreadSize_(4),
	taskSize_(0), 
	taskSizeThreshHold_(TaskMaxThreadHold),
	poolMode_(PoolMode::Mode_FIXED){
	
}
//�淶�����ֹ��죬������������д��
ThreadPool::~ThreadPool() {	}

//�����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}


//�̳߳���ֵ����
void ThreadPool::setTaskQueThreshHold(int threadhold)
{
	taskSizeThreshHold_ = threadhold;
}

//���̳߳��ύ����  ����������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	//��ȡ��
	std::unique_lock<std::mutex> loc(taskQueMtx_);
	//�߳�ͨ�� �ȴ�
	//while (taskQue_.size() == taskSizeThreshHold_) {
	//	notFull_.wait(loc);
	//}
	//����С����������У���������
	if(!(notFull_.wait_for(loc, std::chrono::seconds(1), 
		[&]()->bool {return taskQue_.size() < (size_t)taskSizeThreshHold_; })))
	{
		//�ȴ�һ�룬���������Ȼ������
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp,false); //return ����һ��Result����
	}
	//����п��࣬������ŵ���������й�
	taskQue_.emplace(sp);
	taskSize_++;
	//���񲻿�����notEmptyqȥ����֪ͨ
	notEmpty_.notify_all();
	return Result(sp);
}

//�����̳߳�
void ThreadPool::start(int size = 4)
{
	//��¼�̳߳�ʼ����
	initThreadSize_ = size;
	//�����̶߳���
	for (size_t i = 0; i < initThreadSize_; i++) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		threads_.emplace_back(std::move(ptr));
	}
	//�����߳�
	for (size_t i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
	}
}	

//�������������������
void ThreadPool::threadFunc() {
	for (;;) {
		std::shared_ptr<Task> t;
		{//��ȡ���������������
			std::unique_lock<std::mutex> loc(taskQueMtx_);
			std::cout << std::this_thread::get_id() << "���Ի�ȡ����" << std::endl;
			//���û������������ȴ�
			notEmpty_.wait(loc, [&]()->bool {return taskQue_.size() > 0; });

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
	}
}

///////////////

//���캯����ʵ��
Thread::Thread(ThreadFunc func):func_(func){

}

//��������
Thread::~Thread() {}

//�̷߳���ʵ��
void Thread::start() {
	//����һ���߳�  ִ���̺߳���
	std::thread t1(func_);//cpp11 ��thread_���������������start()������

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
	std::cout << "setResult" << std::endl;
	result_ = res;
}
/////////////////// Result����ʵ��
Result::Result(std::shared_ptr<Task> task, bool isValid):
	task_(task), isValid_(isValid){
	task_->setResult(this);
}

void Result::setVal(Any any) {
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

