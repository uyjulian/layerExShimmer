#include <stdio.h>

#define MAXKTHREADNUM 8

typedef enum { RUNNING, SUSPENDED } KThreadStatus;

// �P�̃X���b�h�̏���ێ�����N���X
template <class T>
class KThread {
	HANDLE		  handle;			// �X���b�h�n���h��
	volatile KThreadStatus stat;		// �X�e�[�^�X(volatile�ŏ����ς����邱�Ƃɒ���)
	T             *instancep;	// run()��instance->funcp(argp)���邽�߂̃C���X�^���X
	void (T::*funcp)(void*);	// �X���b�h���Ŏ��s����֐�
	void*		   argp;		// �X���b�h���Ŏ��s����֐��ɓn������
public:
	KThread(void);		// �R���X�g���N�^
	~KThread();						// �f�X�g���N�^
									// �X���b�h���[�v(static�ł���K�v����)
	static DWORD WINAPI threadLoop(void*);
									// �N���XT�̊֐�func������arg�ŏ����J�n����
	bool run(T *instance, void (T::*funcp)(void*), void *argp, int interval=0);
	bool isRunning()				// ���쒆���ǂ���
	{
		return stat == RUNNING;
	}
	void wait(int interval=0)		// �X���b�h�̏���������҂�
	{
		while(isRunning())
			Sleep(interval);
	}
};

// KThread�N���X�̃R���X�g���N�^
template <class T>
KThread<T>::KThread(/*long *runningsp*/)
{
	handle = CreateThread(NULL, 0, threadLoop, this, 0, NULL);
}

// KThread�N���X�̃f�X�g���N�^
template <class T>
KThread<T>::~KThread()
{
	TerminateThread(handle, 0);	// ������I��点��B�����B
}

// KThread�N���X�̃X���b�h���[�v(static �ł���K�v����)
// ���������̓X���b�h���̂��̂�����Ă���
template <class T>
DWORD WINAPI KThread<T>::threadLoop(void *p)
{
	// �ŏ�CreateThread()�����Ƃ���running��ԂȂ̂ł���ł悢
	KThread<T> *kthread = (KThread<T>*)p;
	while (1) {
		kthread->stat = SUSPENDED;
		SuspendThread(kthread->handle);
		((kthread->instancep)->*(kthread->funcp))(kthread->argp);
	}
}

// �N���XT�̊֐�func������arg�ŏ����J�n����
template <class T>
bool KThread<T>::run(T *instance, void (T::*funcp)(void*), void *argp, int interval=0)
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

// �X���b�h�v�[���N���X
template <class T>
class KThreadPool {
	int			threadnum;					// ���݂̃X���b�h��
	KThread<T>	*threadary[MAXKTHREADNUM-1];// �X���b�h�z��
public:
	KThreadPool(int cpunum=0);				// �R���X�g���N�^
	~KThreadPool();							// �f�X�g���N�^
	void setThreadNum(int num=0);			// �X���b�h����ݒ肷��
	int getThreadNum(void);					// �X���b�h���𓾂�
	void waitForAllThreads(int interval=0);	// �S�X���b�h�̏���������҂�
											// �N���XT�̊֐�func��arg�ŃX���b�h�ɂď����J�n����
	void run(T *instancep, void (T::*funcp)(void*), void *argp);
};

// KThreadPool �R���X�g���N�^
template <class T>
KThreadPool<T>::KThreadPool(int cpunum=0)
{
	threadnum = 0;
	setThreadNum(cpunum);
}

// �f�X�g���N�^
template <class T>
KThreadPool<T>::~KThreadPool()
{
}

// �X���b�h����ݒ肷��Bnum=0�Ȃ�CPU�X���b�h��-1��ݒ肷��
template <class T>
void KThreadPool<T>::setThreadNum(int num=0)
{
	if (num == 0) {
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		num = sysinfo.dwNumberOfProcessors;
	}
	int newthreadnum = min(MAXKTHREADNUM-1, num-1); // -1 �͎������܂ނ���
	if (newthreadnum < 0)
		newthreadnum = 0; // �����������̂�0�͂��肤��

	if (threadnum < newthreadnum) {
		// �X���b�h�v�[�����̃X���b�h�𑝉�������
		for (int i = threadnum; i < newthreadnum; i++)
			threadary[i] = new KThread<T>();
	} else if (threadnum > newthreadnum) {
		// �X���b�h�v�[�����̃X���b�h������������
		for (int i = newthreadnum-1; i >= threadnum; i--) {
			threadary[i]->wait();	// ���̃X���b�h��suspend����̂�҂��Ă���
			delete threadary[i];	// �X���b�h�폜
		}
	}
	threadnum = newthreadnum;
}

// �X���b�h���𓾂�
template <class T>
int KThreadPool<T>::getThreadNum(void)
{
	return threadnum+1;
}


// �S�X���b�h�̏���������҂�
template <class T>
void KThreadPool<T>::waitForAllThreads(int interval=0)
{
	for (int i = 0; i < threadnum; i++)
		threadary[i]->wait(interval);
}

// �N���XT�̊֐�func������arg�ŃX���b�h�ɂď����J�n����
template <class T>
void KThreadPool<T>::run(T *instancep, void (T::*funcp)(void*), void *argp)
{
	for (int i = 0; i < threadnum; i++)
		if (threadary[i]->run(instancep, funcp, argp))
			return;
	// �󂫂��Ȃ������玩���Ŏ��s����
	(instancep->*funcp)(argp);
}
