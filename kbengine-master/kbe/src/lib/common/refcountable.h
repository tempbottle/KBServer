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


/*
	引用计数实现类

	使用方法:
		class AA:public RefCountable
		{
		public:
			AA(){}
			~AA(){ printf("析构"); }
		};
		
		--------------------------------------------
		AA* a = new AA();
		RefCountedPtr<AA>* s = new RefCountedPtr<AA>(a);
		RefCountedPtr<AA>* s1 = new RefCountedPtr<AA>(a);
		
		int i = (*s)->getRefCount();
		
		delete s;
		delete s1;
		
		执行结果:
			析构
*/
#ifndef KBE_REFCOUNTABLE_H
#define KBE_REFCOUNTABLE_H
	
#include "common.h"
	
namespace KBEngine{

class RefCountable 
{
public:
	// 减少函数调用的开销
	inline void incRef(void) const
	{
		++refCount_;
	}

	inline void decRef(void) const
	{
		
		int currRef = --refCount_;
		assert(currRef >= 0 && "RefCountable:currRef maybe a error!");
		if (0 >= currRef)
			onRefOver();											// 引用结束了
	}

	virtual void onRefOver(void) const
	{
		// const_cast转换符是用来移除变量的const或volatile限定符
		delete const_cast<RefCountable*>(this);
	}

	void setRefCount(int n)
	{
		refCount_ = n;
	}

	int getRefCount(void) const 
	{ 
		return refCount_; 
	}

protected:
	// 只能被子类调用
	RefCountable(void) : refCount_(0) 
	{
	}

	// 当用一个基类的指针删除一个派生类的对象时，派生类的析构函数会被调用
	virtual ~RefCountable(void) 
	{ 
		assert(0 == refCount_ && "RefCountable:currRef maybe a error!"); 
	}

protected:
	volatile mutable long refCount_;
}; 

#if KBE_PLATFORM == PLATFORM_WIN32
class SafeRefCountable 
{
public:
	inline void incRef(void) const
	{
		// Increments(increases by one) the value of the specified 32 - bit variable as an atomic operation
		::InterlockedIncrement(&refCount_);
	}

	inline void decRef(void) const
	{
		// Decrements(decreases by one) the value of the specified 32 - bit variable as an atomic operation
		long currRef =::InterlockedDecrement(&refCount_);
		assert(currRef >= 0 && "RefCountable:currRef maybe a error!");
		if (0 >= currRef)
			onRefOver();											// 引用结束了
	}

	virtual void onRefOver(void) const
	{
		delete const_cast<SafeRefCountable*>(this);
	}

	void setRefCount(long n)
	{
		InterlockedExchange((long *)&refCount_, n);
	}

	int getRefCount(void) const 
	{ 
		return InterlockedExchange((long *)&refCount_, refCount_);
	}

protected:
	SafeRefCountable(void) : refCount_(0) 
	{
	}

	virtual ~SafeRefCountable(void) 
	{ 
		assert(0 == refCount_ && "SafeRefCountable:currRef maybe a error!"); 
	}

protected:
	// 在C++中，mutable是为了突破const的限制而设置的。被mutable修饰的变量(mutable只能由于修饰类的非静态数据成员)，将永远处于可变的状态，即使在一个const函数中。
	// 使用volatile 声明的变量的值的时候，系统总是重新从它所在的内存读取数据，即使它前面的指令刚刚从该处读取过数据
	volatile mutable long refCount_;
};
#else
class SafeRefCountable 
{
public:
	inline void incRef(void) const
	{
		__asm__ volatile (
			"lock addl $1, %0"
			:						// no output
			: "m"	(this->refCount_) 	// input: this->count_
			: "memory" 				// clobbers memory
		);
	}

	inline void decRef(void) const
	{
		
		long currRef = intDecRef();
		assert(currRef >= 0 && "RefCountable:currRef maybe a error!");
		if (0 >= currRef)
			onRefOver();											// 引用结束了
	}

	virtual void onRefOver(void) const
	{
		delete const_cast<SafeRefCountable*>(this);
	}

	void setRefCount(long n)
	{
		//InterlockedExchange((long *)&refCount_, n);
	}

	int getRefCount(void) const 
	{ 
		//return InterlockedExchange((long *)&refCount_, refCount_);
		return refCount_;
	}

protected:
	SafeRefCountable(void) : refCount_(0) 
	{
	}

	virtual ~SafeRefCountable(void) 
	{ 
		assert(0 == refCount_ && "SafeRefCountable:currRef maybe a error!"); 
	}

protected:
	volatile mutable long refCount_;
private:
	/**
	 *	This private method decreases the reference count by 1.
	 */
	inline int intDecRef() const
	{
		int ret;
		__asm__ volatile (
			"mov $-1, %0  \n\t"
			"lock xadd %0, %1"
			: "=&a"	(ret)				// output only and early clobber
			: "m"	(this->refCount_)		// input (memory)
			: "memory"
		);
		return ret;
	}
};
#endif

template<class T>
class RefCountedPtr 
{
public:
	RefCountedPtr(T* ptr):ptr_(ptr) 
	{
		if (ptr_)
			ptr_->addRef();
	}

	RefCountedPtr(RefCountedPtr<T>* refptr):ptr_(refptr->getObject()) 
	{
		if (ptr_)
			ptr_->addRef();
	}
	
	~RefCountedPtr(void) 
	{
		if (0 != ptr_)
			ptr_->decRef();
	}

	T& operator*() const 
	{ 
		return *ptr_; 
	}

	T* operator->() const 
	{ 
		return (&**this); 
	}

	T* getObject(void) const 
	{ 
		return ptr_; 
	}

private:
	T* ptr_;
};

}
#endif // KBE_REFCOUNTABLE_H
