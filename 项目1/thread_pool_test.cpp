#include <iostream>
#include "threadpool.h"
#include <thread>
#include <chrono>
#include <memory>

using ULong = unsigned long long;

//任务抽象基类
class Task1 :public Task {
public: 
	Task1(ULong a = 10, ULong b = 20) :num1(a), num2(b) {}
	//如何设计run函数的返回值可以表示任意的类型
	Any run() {
		std::cout << "Task run" << std::endl;
		ULong sum = 0;
		for (ULong i = num1; i <= num2; i++) {
			sum += i;
		}
		std::this_thread::sleep_for(std::chrono::seconds(3));
		return sum;
	}
private:
	ULong num1;
	ULong num2;
};


int main() {
	{
		//线程池析构以后如何回收相关线程资源
		ThreadPool tpool;
		//设置线程池的线程数量以及模式
		tpool.setMode(PoolMode::Mode_CACHED);

		tpool.start(4);

		Result res = tpool.submitTask(std::shared_ptr<Task>(new Task1(1, 100000000)));
		Result res2 = tpool.submitTask(std::shared_ptr<Task>(new Task1(100000001, 200000000)));
		Result res3 = tpool.submitTask(std::shared_ptr<Task>(new Task1(200000001, 300000000)));
		tpool.submitTask(std::shared_ptr<Task>(new Task1(1, 100000000)));
		tpool.submitTask(std::shared_ptr<Task>(new Task1(1, 100000000)));
		tpool.submitTask(std::shared_ptr<Task>(new Task1(1, 100000000)));
		//若用户没有执行完，需要进行阻塞，否则没有返回值
		//随着task被执行完，task对象没来，依赖于task对象的result对象就也没了

		ULong sum1 = res.get().cast_<ULong>();
		ULong sum2 = res2.get().cast_<ULong>();
		ULong sum3 = res3.get().cast_<ULong>();


		//Master-Slave线程模型
		//Master线程分解任务，然后给各个Slave线程分配任务
		//等待各个线程Slave线程执行完成任务后，返回结果
		//Master线程合并各个任务结果并输出
		std::cout << "--------" << sum1 + sum2 + sum3 << "--------" << std::endl;
	}


	std::this_thread::sleep_for(std::chrono::milliseconds(200000));
	return 0;
}