// ����ջ
#include <atomic>
#include <memory>

template <typename T = int>
class LFStack {
	struct Node;
	// ���ڱ�ǽڵ�����ü���
	struct counterNodePtr
	{
		int externalCount;
		Node* ptr;
	};
	struct Node
	{
		std::shared_ptr<T> data;
		std::atomic<int> internalCount;
		counterNodePtr next;
		Node(T const& data_) : data(std::make_shared<T>(data_)), internalCount(0) {}
	};
	std::atomic<counterNodePtr> head;
	void increaseHeadCount(counterNodePtr& oldCounter)
	{
		counterNodePtr newCounter;
		do
		{
			newCounter = oldCounter;
			++newCounter.externalCount;
		} while (!head.compare_exchange_strong(oldCounter, newCounter,
			std::memory_order_acquire, std::memory_order_relaxed));
		oldCounter.externalCount = newCounter.externalCount;
	}
	

	std::atomic<int*> head;
	std::atomic<int*> toBeDeleted;	// ��ɾ����
	std::atomic_uint threadsInPop;

	// �ͷ�����
	static void deleteNodes(Node* nodes)
	{
		while (nodes)
		{
			Node* next = nodes->next;
			delete nodes;
			nodes = next;
		}
	}
	void tryReclaim(Node* oldHead);

	// �����㵽����
	void chainPendingNodes(Node* nodes);
	void chainPendingNodes(Node* first, Node* last);
	void chainPendingNode(Node* node);
public:
	~LFStack()
	{
		while (pop());
	}
	void push(T const& data)
	{
		counterNodePtr newHead;
		newHead.ptr = new Node(data);
		newHead.externalCount = 1;
		newHead.ptr->next = head.load(std::memory_order_relaxed);
		while (!head.compare_exchange_weak(newHead.ptr->next, newHead,
			std::memory_order_release, std::memory_order_relaxed));
	}
	std::shared_ptr<T> pop()
	{
		counterNodePtr oldHead = head.load(std::memory_order_relaxed);
		for (;;)
		{
			increaseHeadCount(oldHead);
			Node* const ptr = oldHead.ptr;
			if (!ptr)
			{
				return std::shared_ptr<T>();
			}
			if (head.compare_exchange_strong(oldHead, ptr->next,
				std::memory_order_relaxed))
			{
				std::shared_ptr<T> res;
				res.swap(ptr->data);
				int const countIncrease = oldHead.externalCount - 2;
				if (ptr->internalCount.fetch_add(countIncrease,
					std::memory_order_release) == -countIncrease)
				{
					delete ptr;
				}
				return res;
			}
			else if (ptr->internalCount.fetch_sub(1,
				std::memory_order_relaxed) == 1)
			{
				delete ptr;
			}
		}
	}
	void push(const T& data)
	{
		Node* const newNode = new Node(data);
		newNode->next = head.load();
		while (!head.compare_exchange_weak(newNode->next, newNode));
	}

	std::shared_ptr<T> pop()
	{
		++threadsInPop;		// ����popʱ�������ü���
		Node* oldHead = head.load();
		while (oldHead &&
			!head.compare_exchange_weak(oldHead, oldHead->next));
		std::shared_ptr<T> res;
		if (oldHead)
		{
			res.swap(oldHead->data);	// �ͷ��ڴ�
		}
		tryReclaim(oldHead);	// ���Ի��սڵ�
		return res;
	}
	LFStack() = default;
};

template<typename T>
inline void LFStack<T>::tryReclaim(Node* oldHead)
{
	if (threadsInPop == 1)
	{
		Node* nodes = toBeDeleted.exchange(nullptr);	// �ӹܺ�ɾ����
		if (!--threadsInPop)
		{
			deleteNodes(nodes);	// �����б��̵߳���popʱ��ɾ����ɾ����
		}
		else if (nodes)
		{
			chainPendingNodes(nodes);	// ���ӹ�����ǰ�����̲߳����˽�㣬��Ҫ������Ż�ȥ�����ܰ�ȫɾ��
		}
		delete oldHead;
	}
	else // �����ǽ����̵߳���pop�����������ɾ������˳�
	{
		chainPendingNodes(oldHead);
		--threadsInPop;
	}
}

template<typename T>
inline void LFStack<T>::chainPendingNodes(Node* nodes)
{
	Node* last = nodes;	
	// �ҵ�β���
	while (Node* const next = last->next)
	{
		last = next;
	}
	chainPendingNodes(nodes, last);
}

template<typename T>
inline void LFStack<T>::chainPendingNodes(Node* first, Node* last)
{
	last->next = toBeDeleted;
	while (!toBeDeleted.compare_exchange_weak(last->next, first)); // ��֤�����ڴ��ڼ䱻�����ɾ����Ľ�㶼�����
}

template<typename T>
inline void LFStack<T>::chainPendingNode(Node* node)
{
	chainPendingNodes(node, node);
}
