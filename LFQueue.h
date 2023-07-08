#pragma once
#include <memory>
#include <atomic>
#include <thread>

template <typename T>
class LFQueue
{
	struct Node;
	struct CountedNodePtr
	{
		int externalCount;
		Node* ptr;
	};

	std::atomic<CountedNodePtr> head;
	std::atomic<CountedNodePtr> tail;
	struct NodeCounter
	{
		unsigned internalCount : 30;
		unsigned externalCount : 2;
	};
	struct Node
	{
		std::atomic<T*> data;
		std::atomic<NodeCounter> count;
		std::atomic<CountedNodePtr> next;
		Node()
		{
			NodeCounter newCount;
			newCount.internalCount = 0;
			newCount.externalCount = 2;
			count.store(newCount);

			next.ptr = nullptr;
			next.externalCount = 0;
		}

		void release_ref()
		{
			NodeCounter oldCounter = count.load(std::memory_order_relaxed);
			NodeCounter newCounter;
			do
			{
				newCounter = oldCounter;
				--newCounter.internalCount;
			} while (!count.compare_exchange_strong(oldCounter, newCounter,
				std::memory_order_acquire, std::memory_order_relaxed));
			if (!newCounter.internalCount &&
				!newCounter.externalCount)
			{
				delete this;
			}
		}
	};

	Node* popHead()
	{
		Node* const oldHead = head.load();
		if (oldHead == tail.load())
		{
			return nullptr;
		}
		head.store(oldHead->next);
		return oldHead;
	}

	void setNewTail(CountedNodePtr& oldTail, const CountedNodePtr& newTail)
	{
		Node* const currentTailPtr = oldTail.ptr;
		while (!tail.compare_exchange_weak(oldTail, newTail) && oldTail.ptr == currentTailPtr);
		if (oldTail.ptr == currentTailPtr)
		{
			free_external_counter(oldTail);
		}
		else
		{
			currentTailPtr->release_ref();
		}
	}

	static void increase_external_count(std::atomic<CountedNodePtr>& counter,
		CountedNodePtr& oldCounter)
	{
		CountedNodePtr newCounter;
		do
		{
			newCounter = oldCounter;
			++newCounter.externalCount;
		} while (!counter.compare_exchange_strong(oldCounter, newCounter, std::memory_order_acquire, std::memory_order_relaxed));
		oldCounter.externalCount = newCounter.externalCount;
	}
public:
	LFQueue()
	{
		head.store(new Node);
		tail.store(head.load());
	}
	LFQueue(const LFQueue&) = delete;
	LFQueue& operator=(const LFQueue&) = delete;
	~LFQueue()
	{
		while (Node* const oldHead = head.load())0.c
		{
			head.store(oldHead->next);
			delete oldHead;
		}
	}
	std::unique_ptr<T> pop()
	{
		CountedNodePtr oldHead = head.load(std::memory_order_relaxed);
		for (;;)
		{
			increase_external_count(head, oldHead);
			Node* const ptr = oldHead.ptr;
			if (ptr == tail.load().ptr)
			{
				return std::unique_ptr<T>();
			}
			if (head.compare_exchange_strong(oldHead, ptr->next))
			{
				std::unique_ptr<T> res;
				res.swap(ptr->data);
				const int count_increase = oldHead.externalCount - 2;
				if (ptr->internalCount.fetch_add(count_increase) == -count_increase)
				{
					delete ptr;
				}
				return res;
			}
			ptr->release_ref();
		}
	}
	
	void push(T new_value)
	{
		std::unique_ptr<T> newData(new T(new_value));
		CountedNodePtr newNext;
		newNext.ptr = new Node;
		newNext.externalCount = 1;
		CountedNodePtr oldTail = tail.load();
		
		for (;;)
		{
			increaseExternalCount(tail, oldTail);
			T* oldData = nullptr;
			if (oldTail.ptr->data.compare_exchange_strong(oldData, newData.get()))
			{
				CountedNodePtr oldNext = { 0 };
				if (!oldTail.ptr->next.compare_exchange_strong(oldNext, newNext))
				{
					delete newNext.ptr;
					newNext = oldNext;
				}
				setNewTail(oldTail, newNext);
				newData.release();
				break;
			}
			else
			{
				CountedNodePtr oldNext = { 0 };
				if (oldTail.ptr->next.compare_exchange_strong(oldNext, newNext))
				{
					oldNext = newNext;
					newNext.ptr = new Node;
				}
				setNewTail(oldTail, oldNext);
			}
		}
	}
};