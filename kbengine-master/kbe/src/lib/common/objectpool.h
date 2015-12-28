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

#ifndef KBE_OBJECTPOOL_H
#define KBE_OBJECTPOOL_H

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <iostream>	
#include <map>	
#include <list>	
#include <vector>
#include <queue> 

#include "thread/threadmutex.h"

namespace KBEngine{

#define OBJECT_POOL_INIT_SIZE	16
#define OBJECT_POOL_INIT_MAX_SIZE	OBJECT_POOL_INIT_SIZE * 16

template< typename T >
class SmartPoolObject;

/*
	一些对象会非常频繁的被创建， 例如：MemoryStream, Bundle, TCPPacket等等
	这个对象池对通过服务端峰值有效的预估提前创建出一些对象缓存起来，在用到的时候直接从对象池中
	获取一个未被使用的对象即可。
*/
template< typename T >
class ObjectPool
{
public:
	typedef std::list<T*> OBJECTS; // 别名

	ObjectPool(std::string name):
		objects_(),
		max_(OBJECT_POOL_INIT_MAX_SIZE),
		isDestroyed_(false),
		mutex_(),
		name_(name),
		total_allocs_(0),
		obj_count_(0)
	{
	}

	ObjectPool(std::string name, unsigned int preAssignVal, size_t max):
		objects_(),
		max_((max == 0 ? 1 : max)),
		isDestroyed_(false),
		mutex_(),
		name_(name),
		total_allocs_(0), // objectpool中的所有对象
		obj_count_(0) // objectpol中闲置的对象数量
	{
	}

	~ObjectPool()
	{
		destroy();
	}	
	
	void destroy()
	{
		mutex_.lockMutex();

		isDestroyed_ = true;

		typename OBJECTS::iterator iter = objects_.begin();
		for(; iter!=objects_.end(); ++iter)
		{
			// 通知池对象,池对象方法destructorPoolObject返回false,不允许delete
			if(!(*iter)->destructorPoolObject())
			{
				delete (*iter);
			}
		}
				
		objects_.clear();	
		obj_count_ = 0;
		mutex_.unlockMutex();
	}

	// const函数,函数不会修改变量,返回的objects_不会被修改
	const OBJECTS& objects(void) const { return objects_; }

	void assignObjs(unsigned int preAssignVal = OBJECT_POOL_INIT_SIZE)
	{
		for(unsigned int i=0; i<preAssignVal; ++i){
			objects_.push_back(new T);
			++total_allocs_;
			++obj_count_;
		}
	}

	/** 
		强制创建一个指定类型的对象。 如果缓冲里已经创建则返回现有的，否则
		创建一个新的， 这个对象必须是继承自T的。
	*/
	template<typename T1>
	T* createObject(void)
	{
		mutex_.lockMutex();

		while(true)
		{
			if(obj_count_ > 0)
			{
				// static_cast < type-id > ( expression )
				// 该运算符把expression转换为type-id类型，但没有运行时类型检查来保证转换的安全性。
				T* t = static_cast<T1*>(*objects_.begin());
				objects_.pop_front();
				--obj_count_;
				t->onEabledPoolObject();
				mutex_.unlockMutex();
				return t;
			}

			assignObjs();
		}

		mutex_.unlockMutex();

		return NULL;
	}

	/** 
		创建一个对象。 如果缓冲里已经创建则返回现有的，否则
		创建一个新的。
	*/
	T* createObject(void)
	{
		mutex_.lockMutex();

		while(true)
		{
			if(obj_count_ > 0)
			{
				T* t = static_cast<T*>(*objects_.begin());
				objects_.pop_front();
				--obj_count_;
				t->onEabledPoolObject();
				mutex_.unlockMutex();
				return t;
			}

			assignObjs();
		}

		mutex_.unlockMutex();

		return NULL;
	}

	/**
		回收一个对象
	*/
	void reclaimObject(T* obj)
	{
		mutex_.lockMutex();
		reclaimObject_(obj);
		mutex_.unlockMutex();
	}

	/**
		回收一个对象容器
	*/
	void reclaimObject(std::list<T*>& objs)
	{
		mutex_.lockMutex();

		typename std::list< T* >::iterator iter = objs.begin();
		for(; iter != objs.end(); ++iter)
		{
			reclaimObject_((*iter));
		}
		
		objs.clear();

		mutex_.unlockMutex();
	}

	/**
		回收一个对象容器
	*/
	void reclaimObject(std::vector< T* >& objs)
	{
		mutex_.lockMutex();

		typename std::vector< T* >::iterator iter = objs.begin();
		for(; iter != objs.end(); ++iter)
		{
			reclaimObject_((*iter));
		}
		
		objs.clear();

		mutex_.unlockMutex();
	}

	/**
		回收一个对象容器
	*/
	void reclaimObject(std::queue<T*>& objs)
	{
		mutex_.lockMutex();

		while(!objs.empty())
		{
			T* t = objs.front();
			objs.pop();
			reclaimObject_(t);
		}

		mutex_.unlockMutex();
	}

	size_t size(void) const{ return obj_count_; }
	
	std::string c_str()
	{
		char buf[1024];

		mutex_.lockMutex();

		sprintf(buf, "ObjectPool::c_str(): name=%s, objs=%d/%d, isDestroyed=%s.\n", 
			name_.c_str(), (int)obj_count_, (int)max_, (isDestroyed ? "true" : "false"));

		mutex_.unlockMutex();

		return buf;
	}

	size_t max() const{ return max_; }
	size_t totalAllocs() const{ return total_allocs_; }

	bool isDestroyed() const{ return isDestroyed_; }

protected:
	/**
		回收一个对象
	*/
	void reclaimObject_(T* obj)
	{
		if(obj != NULL)
		{
			// 先重置状态
			obj->onReclaimObject();

			if(size() >= max_ || isDestroyed_)
			{
				delete obj;
				--total_allocs_;
			}
			else
			{
				objects_.push_back(obj);
				++obj_count_;
			}
		}
	}

protected:
	OBJECTS objects_;

	size_t max_;

	bool isDestroyed_;

	// 一些原因导致锁还是有必要的
	// 例如：dbmgr任务线程中输出log，cellapp中加载navmesh后的线程回调导致的log输出
	KBEngine::thread::ThreadMutex mutex_;

	std::string name_;

	size_t total_allocs_;

	// Linux环境中，list.size()使用的是std::distance(begin(), end())方式来获得
	// 会对性能有影响，这里我们自己对size做一个记录
	size_t obj_count_;
};

/*
	池对象， 所有使用池的对象必须实现回收功能。
*/
class PoolObject
{
public:
	virtual ~PoolObject(){}
	// 回收对象时的清理工作,必须实现
	virtual void onReclaimObject() = 0;
	virtual void onEabledPoolObject(){}

	virtual size_t getPoolObjectBytes(){ return 0; }

	/**
		池对象被析构前的通知
		某些对象可以在此做一些工作
	*/
	virtual bool destructorPoolObject()
	{
		return false;
	}
};

template< typename T >
class SmartObjectPool : public ObjectPool<T>
{
public:
};

// 对poolObject的对象进行封装
template< typename T >
class SmartPoolObject
{
public:
	SmartPoolObject(T* pPoolObject, ObjectPool<T>& objectPool):
	  pPoolObject_(pPoolObject),
	  objectPool_(objectPool)
	{
	}

	~SmartPoolObject()
	{
		onReclaimObject();
	}

	void onReclaimObject()
	{
		if(pPoolObject_ != NULL)
		{
			objectPool_.reclaimObject(pPoolObject_);
			pPoolObject_ = NULL;
		}
	}

	T* get()
	{
		return pPoolObject_;
	}

	T* operator->()
	{
		return pPoolObject_;
	}

	T& operator*()
	{
		return *pPoolObject_;
	}

private:
	T* pPoolObject_;
	ObjectPool<T>& objectPool_;
};


#define NEW_POOL_OBJECT(TYPE) TYPE::ObjPool().createObject();


}
#endif
