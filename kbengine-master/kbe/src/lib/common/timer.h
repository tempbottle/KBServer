/*
This source file is part of KBEngine
For the latest info, see http://www.kbengine.org/

Copyright (c) 2008-2016 KBEngine.

KBEngine is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

KBEngine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.
 
You should have received a copy of the GNU Lesser General Public License
along with KBEngine.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef KBE_TIMER_H
#define KBE_TIMER_H

#include "common/common.h"
#include "common/timestamp.h"
#include "helper/debug_helper.h"

namespace KBEngine
{
class TimersBase;
class TimeBase;

class TimerHandle
{
public:
	explicit TimerHandle(TimeBase * pTime = NULL) : pTime_( pTime ) {}

	void cancel(); // ����TimeBase��cancle����
	void clearWithoutCancel()	{ pTime_ = NULL; }

	bool isSet() const		{ return pTime_ != NULL; }

	friend bool operator==( TimerHandle h1, TimerHandle h2 );
	TimeBase * time() const	{ return pTime_; }

private:
	TimeBase * pTime_;
};

inline bool operator==( TimerHandle h1, TimerHandle h2 )
{
	return h1.pTime_ == h2.pTime_;
}


/**
 *	����̳�����ӿ�
 *	������timer->handleTimeout�¼�
 */
class TimerHandler
{
public:
	TimerHandler() : numTimesRegistered_( 0 ) {}

	/* ͨ�������ָ����ɾ��������Ķ���ʱ,����������������� */
	virtual ~TimerHandler()
	{
		KBE_ASSERT( numTimesRegistered_ == 0 );
	};

	/* ���麯�� */
	virtual void handleTimeout(TimerHandle handle, void * pUser) = 0;

protected:
	/* �麯�� */
	virtual void onRelease( TimerHandle handle, void * pUser ) {}
private:
	friend class TimeBase;

	void incTimerRegisterCount() { ++numTimesRegistered_; }
	void decTimerRegisterCount() { --numTimesRegistered_; }

	void release( TimerHandle handle, void * pUser )
	{
		this->decTimerRegisterCount();
		this->onRelease( handle, pUser );
	}

	int numTimesRegistered_;
};

class TimeBase
{
public:
	/* һ��TimerHandler���Ա����TimeBaseӵ�� */
	TimeBase(TimersBase &owner, TimerHandler* pHandler, 
		void* pUserData);
	
	virtual ~TimeBase(){}

	void cancel();

	void * getUserData() const	{ return pUserData_; }

	bool isCancelled() const{ return state_ == TIME_CANCELLED; }
	bool isExecuting() const{ return state_ == TIME_EXECUTING; }
protected:
	enum TimeState
	{
		TIME_PENDING,
		TIME_EXECUTING,
		TIME_CANCELLED
	};

	TimersBase& owner_;
	TimerHandler * pHandler_;
	void *pUserData_;
	TimeState state_;
};

class TimersBase
{
public:
	virtual void onCancel() = 0;
};

template<class TIME_STAMP>
class TimersT : public TimersBase
{
public:
	typedef TIME_STAMP TimeStamp;

	TimersT();
	virtual ~TimersT();
	
	inline uint32 size() const	{ return timeQueue_.size(); }
	inline bool empty() const	{ return timeQueue_.empty(); }
	
	int	process(TimeStamp now);
	bool legal( TimerHandle handle ) const;
	TIME_STAMP nextExp( TimeStamp now ) const;
	void clear( bool shouldCallCancel = true );
	
	bool getTimerInfo( TimerHandle handle, 
					TimeStamp& time, 
					TimeStamp&	interval,
					void *&	pUser ) const;
	
	/* ���������� */
	TimerHandle	add(TimeStamp startTime, TimeStamp interval,
						TimerHandler* pHandler, void * pUser);
	
private:
	
	typedef std::vector<KBEngine::TimeBase *> Container;
	Container container_;

	void purgeCancelledTimes();
	void onCancel();

	class Time : public TimeBase
	{
	public:
		/*
		* @param startTime {TimeStamp} ��ʼʱ��
		* @param interval {TimeStamp} ʱ����
		*/
		Time( TimersBase & owner, TimeStamp startTime, TimeStamp interval,
			TimerHandler * pHandler, void * pUser );

		TIME_STAMP time() const			{ return time_; } // ��ʼʱ��
		TIME_STAMP interval() const		{ return interval_; } // ʱ����

		void triggerTimer();

	private:
		TimeStamp			time_;
		TimeStamp			interval_;

		/*
		* ˽�й��캯��,��ʾ�����������캯��
		*/
		Time( const Time & );
		Time & operator=( const Time & );
	};

	/* �Ƚ϶�ʱ���Ĵ���ʱ��,��������TRUE */
	class Comparator
	{
	public:
		bool operator()(const Time* a, const Time* b)
		{
			return a->time() > b->time();
		}
	};
	
	/* ��ʱ������(�������ȼ�����) */
	class PriorityQueue
	{
	public:
		typedef std::vector<Time *> Container;

		/* ���� */
		typedef (typename Container::value_type) value_type;
		typedef (typename Container::size_type) size_type;

		bool empty() const				{ return container_.empty(); }
		size_type size() const			{ return container_.size(); }

		const value_type & top() const	{ return container_.front(); }

		/* ���ݶ�ʱ������ʱ����Ⱥ�˳��,PUSH��Container�� */
		void push( const value_type & x )
		{
			container_.push_back( x );
			std::push_heap( container_.begin(), container_.end(),
					Comparator() );
		}

		/* ���ݶ�ʱ������ʱ����Ⱥ�˳��,��Container��POP��ʱ�� */
		void pop()
		{
			std::pop_heap( container_.begin(), container_.end(), Comparator() );
			container_.pop_back();
		}

		Time * unsafePopBack()
		{
			Time * pTime = container_.back();
			/* Delete last element */
			container_.pop_back();
			return pTime;
		}

		Container & container()		{ return container_; }

		void make_heap()
		{
			/* Rearranges the elements in the range [first,last) in such a way that they form a heap */
			std::make_heap( container_.begin(), container_.end(),
					Comparator() );
		}

	private:
		Container container_;
	};
	
	PriorityQueue	timeQueue_;
	Time * 			pProcessingNode_;
	TimeStamp 		lastProcessTime_;
	int				numCancelled_;

	TimersT( const TimersT & );
	TimersT & operator=( const TimersT & );

};

typedef TimersT<uint32> Timers;
typedef TimersT<uint64> Timers64;
}

#ifdef CODE_INLINE
#include "timer.inl"
#endif
#endif // KBE_TIMER_H
