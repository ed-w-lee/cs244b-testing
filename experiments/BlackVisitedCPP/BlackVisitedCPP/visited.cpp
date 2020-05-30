#include "visited.h"

void Visited::start_txn(std::list<std::string> &traces){
    end_txn();
    for(const std::string& token : traces){
        int childId = add_token(token);
        if (childId > 0){
            register_child(childId);
        }
    }
}

// takes child of current node to continue on txn
// increments the child's count by 1
//
// (in txn, called immediately after get_counts)
// moves to given child node
void Visited::register_child(int child){
    currentNode = currentNode->addElement(child);
}

// returns map from syscall -> count
//
// (in txn, called after register_node or register_syscall)
std::unordered_map<int, size_t>* Visited::get_counts(){
    return currentNode->get_counts();
}

// (in txn, ends the txn)
void Visited::end_txn(){
    currentNode = rootNode;
}

// (either file descriptor or string, whatever you prefer)
// writes the currently explored paths to some file (+ counts, ideally)
// can be in any format, we can do post-processing to extract usable data out
// of it
void Visited::write_paths(std::string out_file){

}

int Visited::add_token(const std::string& token){
    if (last_token_id > max_val){
        return 0;
    }

    if (input_tokens_map.find(token) == input_tokens_map.end()){
        input_tokens_map[token] = last_token_id++;
    }

    return input_tokens_map[token];
}