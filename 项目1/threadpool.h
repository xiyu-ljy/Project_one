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
	�̳߳��������̳߳����飬�洢�̳߳����������ȴ����̳߳������̣߳�
	�����̺߳�ͨ��submitTask���̳߳���������񣬴�ʱ��Ҫ�����߳�ͨ��
	���߳�ͬ������Ҫ��֤threadFuncȡ����ʱ�����������������Submitʱ
	��Ҫ��֤������л�û������
	Ϊ��ȡ���̵߳ķ���ֵ�����ڸ÷���ֵ���û�����������Ҫʵ��һ��Any��������
	�������͵����ݣ�submit֮�󷵻�һ����Any���ݵ�Result�࣬ͬʱ��Result���
	���󴴽����ύ����ʱ���������ɹ������������ͣ����򷵻ؿ�
*/

//Task���͵�ǰ������
class Task;
//���������������ݵ�����
class Any {
public:
	Any() = default;
	//��������0
	template<typename T>
	Any(T data):base_(std::make_unique<Derive<T>>(data)){}

	//��ȡDerive�е�����
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

//ʵ��һ���ź���
class Semaphore {
public:
	Semaphore(int limit = 0) :resLimit_(limit) {};
	~Semaphore() {};

	//�ź�����P����
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
	//�ź�����Դ��
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;

};

//ʵ�ֽ����ύ��������е�����ִ�����ķ���ֵ
class Result {
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;
	
	//setVal �ķ�������ȡ���񷵻�ֵ
	void setVal(Any any);
	//�ṩһ��get�������û����û�ȡtask�ķ���ֵ
	Any get();


private:
	//�洢����ķ���ֵ
	Any res_;
	Semaphore sem_;
	//���task����
	std::shared_ptr<Task> task_;//ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_;  //����ֵ�Ƿ���Ч
};

//�̳߳�ģʽ
enum class PoolMode {
	Mode_FIXED,  //�̶��߳�����
	Mode_CACHED,  //�߳������ɶ�̬����
};

//�߳�����
class Thread {
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();//�߳�����
	//�߳���������
	void start();

	//��ȡ�߳�id
	int getId() const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId; //�����߳�id
};
//����������
class Task {
public:
	Task();
	~Task() =default;
	void exec();
	//�����Ӧ��Result����
	void setResult(Result* res);
	
	virtual Any run() = 0;
public:
	//result�������ڱ�Task������ָ�뼴��
	Result* result_;
};

/*
* 
	example:
	//task����
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
//�̳߳�����
class ThreadPool {
public:
	ThreadPool();
	~ThreadPool();

	//�����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	//�����̳߳�
	void start(int size);

	//�̳߳���ֵ����
	void setTaskQueThreshHold(int threadhold);
	void setThreadSizeThreshHold(int threadhold);


	//���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;
	


private:
	//�̺߳��� �Ӷ�������������
	void threadFunc(int threadId);

	//����̳߳ص�����״̬
	bool checkRunning()const;
private:
	//�߳��б�
	//std::vector<std::unique_ptr<Thread>> threads_;
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;
	
	//��ʼ�̸߳���
	size_t initThreadSize_;
	//��¼�̳߳������߳�����
	std::atomic_int curThreadNum_;
	//��¼�����̵߳�����
	std::atomic_int freeThread;
	//��ʼ���������
	std::queue<std::shared_ptr<Task>> taskQue_;
	//�������
	std::atomic_int taskSize_;
	//����������ֵ,����
	int taskSizeThreshHold_;
	//�߳���������
	int threadSizeThreshHold_;

	std::mutex taskQueMtx_;//��֤��������̰߳�ȫ
	std::condition_variable notFull_;//��֤������в���
	std::condition_variable notEmpty_;//��֤������в���
	std::condition_variable exit_;


	PoolMode poolMode_;//����ģʽ

	std::atomic_bool isStart;//��ʾ��ǰ�̳߳ص�����״̬


};

#endif
