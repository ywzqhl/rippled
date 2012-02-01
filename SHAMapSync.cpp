
#include <stack>

#include "SHAMap.h"

void SHAMap::getMissingNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	
	if(root->isFullBelow())
	{
#ifdef DEBUG
		std::cerr << "getMissingNodes: root is full below" << std::endl;
#endif
		return;
	}

	std::stack<SHAMapInnerNode::pointer> stack;
	stack.push(root);
	while( (max>0) && (!stack.empty()) )
	{
		SHAMapInnerNode::pointer node=stack.top();
		stack.pop();
		bool all_leaves=true;
		for(int i=0; i<32; i++)
		{
			if(!node->isEmptyBranch(i))
			{
#ifdef DEBUG
				std::cerr << "gMN: " << node->getString() << " has non-empty branch " << i << std::endl;
#endif
				if(node->isChildLeaf())
				{ // do we have this leaf node?
					SHAMapLeafNode::pointer leaf=getLeaf(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!leaf)
					{
						if(max-->0)
						{
#ifdef DEBUG
							std::cerr << "gMN: need leaf " << i << std::endl;
#endif
							nodeIDs.push_back(node->getChildNodeID(i));
							hashes.push_back(node->getChildHash(i));
						}
						all_leaves=false;
					}
				}
				else
				{ // do we have this inner node?
					SHAMapInnerNode::pointer desc=getInner(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!desc)
					{
						if(max-->0)
						{
							nodeIDs.push_back(node->getChildNodeID(i));
							hashes.push_back(node->getChildHash(i));
						}
						all_leaves=false;
					}
					else if(!desc->isFullBelow())
					{
#ifdef DEBUG
						std::cerr << "gMN: iterate " << desc->getString() << std::endl;
#endif
						stack.push(desc);
					}
				}
			}
		}
		if(all_leaves) node->setFullBelow();
	}
}

bool SHAMap::getNodeFat(const SHAMapNode& wanted, std::vector<SHAMapNode>& nodeIDs,
	std::list<std::vector<unsigned char> >& rawNodes)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(wanted.isLeaf())
	{ // no fat way to get a leaf
		SHAMapLeafNode::pointer leaf=getLeafNode(wanted);
		if(!leaf) return false;
		nodeIDs.push_back(*leaf);
		Serializer s;
		leaf->addRaw(s);
		rawNodes.push_back(s.peekData());
		return true;
	}

	SHAMapInnerNode::pointer node=getInnerNode(wanted);
	if(!node) return false;

	bool ret=true;
	for(int i=0; i<32; i++)
	{
		if(!node->isEmptyBranch(i))
		{
		 	if(node->isChildLeaf())
		 	{
		 		SHAMapLeafNode::pointer leaf=getLeaf(node->getChildNodeID(i), node->getChildHash(i), false);
		 		if(!leaf) ret=false;
		 		else
		 		{
		 			nodeIDs.push_back(*leaf);
		 			Serializer s;
		 			leaf->addRaw(s);
		 			rawNodes.push_back(s.peekData());
		 		}
		 	}
		 	else
		 	{
		 		SHAMapInnerNode::pointer ino=getInner(node->getChildNodeID(i), node->getChildHash(i), false);
		 		if(!ino) ret=false;
		 		else
		 		{
		 			nodeIDs.push_back(*ino);
		 			Serializer s;
		 			ino->addRaw(s);
		 			rawNodes.push_back(s.peekData());
		 		}
		 	}
		}
	}
	return ret;
}

bool SHAMap::addRootNode(const std::vector<unsigned char>& rootNode)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if(root->getNodeHash()!=0)
	{
#ifdef DEBUG
		std::cerr << "got root node, already have one" << std::endl;
#endif
		return true;
	}

	SHAMapInnerNode::pointer node=SHAMapInnerNode::pointer(new SHAMapInnerNode(SHAMapNode(), rootNode, 0));
	if(!node) return false;

	root=node;
	mInnerNodeByID[*node]=node;
	if(mDirtyInnerNodes) (*mDirtyInnerNodes)[*node]=node;
	return true;
}

bool SHAMap::addRootNode(const uint256& hash, const std::vector<unsigned char>& rootNode)
{
	boost::recursive_mutex::scoped_lock sl(mLock);

	// we already have a root node
	if(root->getNodeHash()!=0)
	{
#ifdef DEBUG
		std::cerr << "got root node, already have one" << std::endl;
#endif
		assert(root->getNodeHash()==hash);
		return true;
	}

	SHAMapInnerNode::pointer node=SHAMapInnerNode::pointer(new SHAMapInnerNode(SHAMapNode(), rootNode, 0));
	if(!node) return false;
	if(node->getNodeHash()!=hash) return false;

	root=node;
	mInnerNodeByID[*node]=node;
	if(mDirtyInnerNodes) (*mDirtyInnerNodes)[*node]=node;
	return true;
}

bool SHAMap::addKnownNode(const SHAMapNode& node, const std::vector<unsigned char>& rawNode)
{ // return value: true=okay, false=error
	assert(!node.isRoot());

	boost::recursive_mutex::scoped_lock sl(mLock);

	if(node.isLeaf())
	{
		if(checkCacheLeaf(node)) return true;
	}
	else
	{
		if(checkCacheNode(node)) return true;
	}

	SHAMapInnerNode::pointer iNode=walkTo(node);
	if(!iNode)
	{	// we should always have a root
		assert(false);
		return true;
	}

	if(iNode->getDepth()==node.getDepth())
	{
#ifdef DEBUG
		std::cerr << "got inner node, already had it (late)" << std::endl;
#endif
		return true;
	}

	if(iNode->getDepth()!=(node.getDepth()-1))
	{ // Either this node is broken or we didn't request it
#ifdef DEBUG
		std::cerr << "got inner node, unable to hook it" << std::endl;
#endif
		return false;
	}

	int branch=iNode->selectBranch(node.getNodeID());
	if(branch<0)
	{
		assert(false);
		return false;
	}
	uint256 hash=iNode->getChildHash(branch);
	if(!hash) return false;

	if(node.isLeaf())
	{ // leaf node
		SHAMapLeafNode::pointer leaf=SHAMapLeafNode::pointer(new SHAMapLeafNode(node, rawNode, mSeq));
		if( (leaf->getNodeHash()!=hash) || (node!=(*leaf)) )
		{
#ifdef DEBUG
			std::cerr << "leaf fails consistency check" << std::endl;
#endif
			return false;
		}
		mLeafByID[node]=leaf;
		if(mDirtyLeafNodes) (*mDirtyLeafNodes)[node]=leaf;
		return true;
	}
	
	SHAMapInnerNode::pointer newNode=SHAMapInnerNode::pointer(new SHAMapInnerNode(node, rawNode, mSeq));
	if( (newNode->getNodeHash()!=hash) || (node!=newNode->getNodeID()) )
	{
#ifdef DEBUG
			std::cerr << "inner node fails consistency check" << std::endl;
#endif
		return false;
	}
	mInnerNodeByID[node]=newNode;
	if(mDirtyInnerNodes) (*mDirtyInnerNodes)[node]=newNode;
	return true;
}

bool SHAMap::deepCompare(SHAMap& other)
{ // Intended for debug/test only
	std::stack<SHAMapInnerNode::pointer> stack;
	boost::recursive_mutex::scoped_lock sl(mLock);

	stack.push(root);
	while(!stack.empty())
	{
		SHAMapInnerNode::pointer node=stack.top();
		stack.pop();

		SHAMapInnerNode::pointer otherNode;
		if(node->isRoot()) otherNode=other.root;
		else otherNode=other.getInner(*node, node->getNodeHash(), false);

		if(!otherNode)
		{
			std::cerr << "unable to fetch node" << std::endl;
			return false;
		}
		else if(otherNode->getNodeHash()!=node->getNodeHash())
		{
			std::cerr << "node hash mismatch" << std::endl;
			return false;
		}

#ifdef DEBUG
		std::cerr << "Comparing inner nodes " << node->getString() << std::endl;
#endif

		for(int i=0; i<32; i++)
		{
			if(node->isEmptyBranch(i))
			{
				if(!otherNode->isEmptyBranch(i))
					return false;
			}
			else
			{
				if(node->isChildLeaf())
				{ //
					SHAMapLeafNode::pointer leaf=getLeaf(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!leaf)
					{
						std::cerr << "unable to fetch leaf" << std::endl;
						return false;
					}
					SHAMapLeafNode::pointer otherLeaf=other.getLeaf(*leaf, leaf->getNodeHash(), false);
					if(!otherLeaf)
					{
						std::cerr << "unable to fetch other leaf" << std::endl;
						return false;
					}
					if(leaf->getNodeHash()!=otherLeaf->getNodeHash())
					{
						std::cerr << "leaf hash mismatch" << std::endl;
						return false;
					}
				}
				else
				{ // do we have this inner node?
					SHAMapInnerNode::pointer next=getInner(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!next)
					{
						std::cerr << "unable to fetch inner node" << std::endl;
						return false;
					}
					stack.push(next);
				}
			}
		}
	}
	return true;
}

bool SHAMap::syncTest()
{
	SHAMap source, destination;

	// add random data to the source map
	int items=10+rand()%400;
	for(int i=0; i<items; i++)
	{
		Serializer s;
		int dlen=rand()%30+4;
		for(int d=0; d<dlen; d++)
			s.add32(rand());
		source.addItem(SHAMapItem(s.getSHA512Half(), s.peekData()));		
	}
	source.setImmutable();

	std::vector<SHAMapNode> nodeIDs, gotNodeIDs;
	std::list<std::vector<unsigned char> > gotNodes;
	std::vector<uint256> hashes;

	std::vector<SHAMapNode>::iterator nodeIDIterator;
	std::list<std::vector<unsigned char> >::iterator rawNodeIterator;

	int passes=0;
	int nodes=0;

	destination.setSynching();

	if(!source.getNodeFat(SHAMapNode(), nodeIDs, gotNodes))
	{
		std::cerr << "GetNodeFat(root) fails" << std::endl;
		assert(false);
		return false;
	}
	if(gotNodes.size()!=1)
	{
		std::cerr << "Didn't get root node" << std::endl;
		assert(false);
		return false;
	}
	if(!destination.addRootNode(*gotNodes.begin()))
	{
		std::cerr << "AddRootNode fails" << std::endl;
		assert(false);
		return false;
	}
	nodeIDs.clear();
	gotNodes.clear();

	do
	{
		passes++;
		hashes.clear();

		// get the list of nodes we know we need
		source.getMissingNodes(nodeIDs, hashes, 128);
		if(!nodeIDs.size()) break;
		
		// get as many nodes as possible based on this information
		for(nodeIDIterator=nodeIDs.begin(); nodeIDIterator!=nodeIDs.end(); ++nodeIDIterator)
			if(!source.getNodeFat(*nodeIDIterator, nodeIDs, gotNodes))
			{
				std::cerr << "GetNodeFat fails" << std::endl;
				assert(false);
				return false;
			}
		nodeIDs.clear();
		hashes.clear();

		if(!gotNodeIDs.size())
		{
			std::cerr << "No nodes gotten" << std::endl;
			assert(false);
			return false;
		}

		for(nodeIDIterator=gotNodeIDs.begin(), rawNodeIterator=gotNodes.begin();
				nodeIDIterator!=gotNodeIDs.end(); ++nodeIDIterator, ++rawNodeIterator)
		{
			nodes++;
			if(!destination.addKnownNode(*nodeIDIterator, *rawNodeIterator))
			{
				std::cerr << "AddKnownNode fails" << std::endl;
				assert(false);
				return false;
			}
		}
		nodeIDs.clear();
		gotNodes.clear();


	} while(1);
	destination.clearSynching();

	if(!source.deepCompare(destination))
	{
		std::cerr << "DeepCompare fails" << std::endl;
		assert(false);
		return false;
	}

#ifdef DEBUG
	std::cerr << "SHAMapSync test passed: " << items << " items, " <<
		passes << " passes, " << nodes << " nodes" << std::endl;
#endif
	
	return true;
}
