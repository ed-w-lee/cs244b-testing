#include "MapTreeNode.h"

MapTreeNode* MapTreeNode::addElement(const int childId){
    MapTreeNode* child = NULL;
    if (mapMemory.find(childId) == mapMemory.end()){
        child = new MapTreeNode(idMaxSize);
        mapMemory[childId] = child;
    } else {
        child = mapMemory[childId];
    }
    child->inc();
    return child;
}

int MapTreeNode::inc(){
    return ++count;
}

MapTreeNode* MapTreeNode::getElement(const int childId){
    if (mapMemory.find(childId) == mapMemory.end()){
        return NULL;
    }

    return mapMemory[childId];
}

std::unordered_map<int, size_t>* MapTreeNode::get_counts(){
    std::unordered_map<int, size_t>* res = new std::unordered_map<int, size_t>();
    for (std::pair<int, MapTreeNode*> node : mapMemory){
        res->insert({{node.first, node.second->get_count()}});
    }
    return res;
}

int MapTreeNode::get_count(){
    return count;
}