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

namespace KBEngine { 

template<class TIME_STAMP>
TimersT<TIME_STAMP>::TimersT():
	timeQueue_(),
	pProcessingNode_( NULL ),
	lastProcessTime_( 0 ),
	numCancelled_( 0 )
{
}

template<class TIME_STAMP>
TimersT<TIME_STAMP>::~TimersT()
{
	this->clear();
}

template <class TIME_STAMP>
TimerHandle TimersT< TIME_STAMP >::add( TimeStamp startTime,
		TimeStamp interval, TimerHandler * pHandler, void * pUser )
{
	Time * pTime = new Time( *this, startTime, interval, pHandler, pUser );
	timeQueue_.push( pTime );
	return TimerHandle( pTime );
}

template <class TIME_STAMP>
void TimersT< TIME_STAMP >::onCancel()
{
	++numCancelled_;

	// If there are too many cancelled timers in the queue (more than half),
	// these are flushed from the queue immediately.

	if (numCancelled_ * 2 > int(timeQueue_.size()))
	{
		this->purgeCancelledTimes();
	}
}

template <class TIME_STAMP>
void TimersT< TIME_STAMP >::clear(bool shouldCallCancel)
{
	int maxLoopCount = timeQueue_.size();

	while (!timeQueue_.empty())
	{
		Time * pTime = timeQueue_.unsafePopBack();
		if (!pTime->isCancelled() && shouldCallCancel)
		{
			--numCancelled_;
			pTime->cancel();

			if (--maxLoopCount == 0)
			{
				shouldCallCancel = false;
			}
		}
		else if (pTime->isCancelled())
		{
			--numCancelled_;
		}

		delete pTime;
	}

	numCancelled_ = 0;
	timeQueue_ = PriorityQueue();
}

template <class TIME>
class IsNotCancelled
{
public:
	bool operator()( const TIME * pTime )
	{
		return !pTime->isCancelled();
	}
};

template <class TIME_STAMP>
void TimersT< TIME_STAMP >::purgeCancelledTimes()
{
	/* TimeBase容器 */
	typename PriorityQueue::Container & container = timeQueue_.container();
	/* 隔离被取消的定时器 */
	typename PriorityQueue::Container::iterator newEnd =
		std::partition( container.begin(), container.end(),
			IsNotCancelled< Time >() );

	/* 销毁被取消的定时器 */
	for (typename PriorityQueue::Container::iterator iter = newEnd;
		iter != container.end();
		++iter)
	{
		delete *iter;
	}

	const int numPurged = (container.end() - newEnd);
	numCancelled_ -= numPurged;
	KBE_ASSERT( (numCancelled_ == 0) || (numCancelled_ == 1) );
	
	container.erase( newEnd, container.end() );
	/* 对timeQueue中的对象进行排序(用堆排序) */
	timeQueue_.make_heap();
}

template <class TIME_STAMP>
int TimersT< TIME_STAMP >::process(TimeStamp now)
{
	int numFired = 0;

	/* 判断定时器队列长度,定时器超时或者被取消，一次处理所有的定时器 */
	while ((!timeQueue_.empty()) && (
		timeQueue_.top()->time() <= now ||
		timeQueue_.top()->isCancelled()))
	{
		/* 获取Time对象(继承TimeBase) */
		Time * pTime = pProcessingNode_ = timeQueue_.top();
		timeQueue_.pop();

		if (!pTime->isCancelled())
		{
			++numFired;
			/* 调用triggerTimer函数(调用handleTimeout) */
			pTime->triggerTimer();
		}

		if (!pTime->isCancelled())
		{
			timeQueue_.push( pTime );
		}
		else
		{
			delete pTime;

			KBE_ASSERT( numCancelled_ > 0 );
			--numCancelled_;
		}
	}

	pProcessingNode_ = NULL;
	lastProcessTime_ = now;
	return numFired;
}

template <class TIME_STAMP>
bool TimersT< TIME_STAMP >::legal(TimerHandle handle) const
{
	typedef Time * const * TimeIter;
	/* 返回TimeBase对象 */
	Time * pTime = static_cast< Time* >( handle.time() );

	if (pTime == NULL)
	{
		return false;
	}

	/* 定时器已经触发并且还没有执行完毕 */
	if (pTime == pProcessingNode_)
	{
		return true;
	}

	TimeIter begin = &timeQueue_.top();
	TimeIter end = begin + timeQueue_.size();
	/* 定时器没有触发 */
	for (TimeIter it = begin; it != end; ++it)
	{
		if (*it == pTime)
		{
			return true;
		}
	}

	return false;
}

template <class TIME_STAMP>
TIME_STAMP TimersT< TIME_STAMP >::nextExp(TimeStamp now) const
{
	if (timeQueue_.empty() ||
		now > timeQueue_.top()->time())
	{
		return 0;
	}
	/* 返回离下一次出发需要等待的时间 */
	return timeQueue_.top()->time() - now;
}

template <class TIME_STAMP>
bool TimersT< TIME_STAMP >::getTimerInfo( TimerHandle handle,
					TimeStamp &			time,
					TimeStamp &			interval,
					void * &			pUser ) const
{
	Time * pTime = static_cast< Time * >( handle.time() );

	/* 获取定时器触发时间,触发间隔,定时器用户信息 */
	if (!pTime->isCancelled())
	{
		time = pTime->time();
		interval = pTime->interval();
		pUser = pTime->getUserData();

		return true;
	}

	return false;
}


inline TimeBase::TimeBase(TimersBase & owner, TimerHandler * pHandler, void * pUserData) :
	owner_(owner),
	pHandler_(pHandler),
	pUserData_(pUserData),
	state_(TIME_PENDING)
{
	pHandler->incTimerRegisterCount();
}

inline void TimeBase::cancel()
{
	if (this->isCancelled()){
		return;
	}

	KBE_ASSERT((state_ == TIME_PENDING) || (state_ == TIME_EXECUTING));
	state_ = TIME_CANCELLED;

	if (pHandler_){
		pHandler_->release(TimerHandle(this), pUserData_);
		pHandler_ = NULL;
	}

	owner_.onCancel();
}


template <class TIME_STAMP>
TimersT< TIME_STAMP >::Time::Time( TimersBase & owner,
		TimeStamp startTime, TimeStamp interval,
		TimerHandler * _pHandler, void * _pUser ) :
	TimeBase(owner, _pHandler, _pUser),
	time_(startTime),
	interval_(interval)
{
}

template <class TIME_STAMP>
void TimersT< TIME_STAMP >::Time::triggerTimer()
{
	/* 定时器没有被取消并且定时器被触发 */
	if (!this->isCancelled())
	{
		state_ = TIME_EXECUTING;

		pHandler_->handleTimeout( TimerHandle( this ), pUserData_ );
		/* 没有时间间隔,执行完FUN后,CANCEL */
		if ((interval_ == 0) && !this->isCancelled())
		{
			this->cancel();
		}
	}

	/* 定时器没有被取消,重新设置触发时间,定时器设置为等待状态 */
	if (!this->isCancelled())
	{
		time_ += interval_;
		state_ = TIME_PENDING;
	}
}

}
