#include <stdio.h>

#define MAXKTHREADNUM 8

typedef enum { RUNNING, SUSPENDED } KThreadStatus;

// １つのスレッドの情報を保持するクラス
template <class T>
class KThread {
	HANDLE		  handle;			// スレッドハンドラ
	volatile KThreadStatus stat;		// ステータス(volatileで書き変えられることに注意)
	T             *instancep;	// run()でinstance->funcp(argp)するためのインスタンス
	void (T::*funcp)(void*);	// スレッド中で実行する関数
	void*		   argp;		// スレッド中で実行する関数に渡す引数
public:
	KThread(void);		// コンストラクタ
	~KThread();						// デストラクタ
									// スレッドループ(staticである必要あり)
	static DWORD WINAPI threadLoop(void*);
									// クラスTの関数funcを引数argで処理開始する
	bool run(T *instance, void (T::*funcp)(void*), void *argp, int interval=0);
	bool isRunning()				// 動作中かどうか
	{
		return stat == RUNNING;
	}
	void wait(int interval=0)		// スレッドの処理完了を待つ
	{
		while(isRunning())
			Sleep(interval);
	}
};

// KThreadクラスのコンストラクタ
template <class T>
KThread<T>::KThread(/*long *runningsp*/)
{
	handle = CreateThread(NULL, 0, threadLoop, this, 0, NULL);
}

// KThreadクラスのデストラクタ
template <class T>
KThread<T>::~KThread()
{
	TerminateThread(handle, 0);	// 無理矢理終わらせる。酷い。
}

// KThreadクラスのスレッドループ(static である必要あり)
// ここだけはスレッドそのものが回っている
template <class T>
DWORD WINAPI KThread<T>::threadLoop(void *p)
{
	// 最初CreateThread()されるときはrunning状態なのでこれでよい
	KThread<T> *kthread = (KThread<T>*)p;
	while (1) {
		kthread->stat = SUSPENDED;
		SuspendThread(kthread->handle);
		((kthread->instancep)->*(kthread->funcp))(kthread->argp);
	}
}

// クラスTの関数funcを引数argで処理開始する
template <class T>
bool KThread<T>::run(T *instance, void (T::*funcp)(void*), void *argp, int interval)
{
	if (isRunning())
		return false;
	this->instancep = instancep;
	this->funcp     = funcp;
	this->argp      = argp;
	this->stat      = RUNNING;
	while (ResumeThread(handle) == 0)
		Sleep(interval);
	return true;
}


// --------------------------------------------------------------------

// スレッドプールクラス
template <class T>
class KThreadPool {
	int			threadnum;					// 現在のスレッド数
	KThread<T>	*threadary[MAXKTHREADNUM-1];// スレッド配列
public:
	KThreadPool(int cpunum=0);				// コンストラクタ
	~KThreadPool();							// デストラクタ
	void setThreadNum(int num=0);			// スレッド数を設定する
	int getThreadNum(void);					// スレッド数を得る
	void waitForAllThreads(int interval=0);	// 全スレッドの処理完了を待つ
											// クラスTの関数funcをargでスレッドにて処理開始する
	void run(T *instancep, void (T::*funcp)(void*), void *argp);
};

// KThreadPool コンストラクタ
template <class T>
KThreadPool<T>::KThreadPool(int cpunum)
{
	threadnum = 0;
	setThreadNum(cpunum);
}

// デストラクタ
template <class T>
KThreadPool<T>::~KThreadPool()
{
}

// スレッド数を設定する。num=0ならCPUスレッド数-1を設定する
template <class T>
void KThreadPool<T>::setThreadNum(int num)
{
	if (num == 0) {
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		num = sysinfo.dwNumberOfProcessors;
	}
	int newthreadnum = std::min(MAXKTHREADNUM-1, num-1); // -1 は自分を含むため
	if (newthreadnum < 0)
		newthreadnum = 0; // 自分を除くので0はありうる

	if (threadnum < newthreadnum) {
		// スレッドプール中のスレッドを増加させる
		for (int i = threadnum; i < newthreadnum; i++)
			threadary[i] = new KThread<T>();
	} else if (threadnum > newthreadnum) {
		// スレッドプール中のスレッドを減少させる
		for (int i = newthreadnum-1; i >= threadnum; i--) {
			threadary[i]->wait();	// そのスレッドがsuspendするのを待ってから
			delete threadary[i];	// スレッド削除
		}
	}
	threadnum = newthreadnum;
}

// スレッド数を得る
template <class T>
int KThreadPool<T>::getThreadNum(void)
{
	return threadnum+1;
}


// 全スレッドの処理完了を待つ
template <class T>
void KThreadPool<T>::waitForAllThreads(int interval)
{
	for (int i = 0; i < threadnum; i++)
		threadary[i]->wait(interval);
}

// クラスTの関数funcを引数argでスレッドにて処理開始する
template <class T>
void KThreadPool<T>::run(T *instancep, void (T::*funcp)(void*), void *argp)
{
	for (int i = 0; i < threadnum; i++)
		if (threadary[i]->run(instancep, funcp, argp))
			return;
	// 空きがなかったら自分で実行する
	(instancep->*funcp)(argp);
}
