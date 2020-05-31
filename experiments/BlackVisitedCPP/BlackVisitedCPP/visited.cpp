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
    try{
        std::ofstream out_file_stream;
        out_file_stream.open (out_file);
        if (out_file_stream.is_open()){
            out_file_stream << input_tokens_map.size() << std::endl;
            for (std::pair<std::string, int> v : input_tokens_map){
                out_file_stream << v.first << FILE_VALS_DELIMETER << v.second << "\n";
            }
            
            std::list<std::pair<std::unordered_map<int, MapTreeNode*>::iterator, std::unordered_map<int, MapTreeNode*>::iterator>> history;
            history.push_back(rootNode->get_iterators());
            bool isRollback = false;
            while (!history.empty())
            {
                //std::cout << "DEBUG: " << history.size() << "\n";
                if (isRollback){
                    history.back().first++;
                }
                if (history.back().first == history.back().second){
                    history.pop_back();
                    if (!isRollback){
                        out_file_stream  << "\n";
                        for( std::pair<std::unordered_map<int, MapTreeNode*>::iterator, std::unordered_map<int, MapTreeNode*>::iterator> v : history){
                            if (v.first != v.second){
                                //TODO: not sure we need number, can just give all routes
                                //out_file_stream << (v.first)->first << FILE_VALS_DELIMETER << (v.first)->second->get_count() << "\n";
                                out_file_stream << (v.first)->first << "\n";
                            }
                        }
                    }
                    isRollback = true;
                } else {
                    isRollback = false;
                    history.push_back((history.back().first)->second->get_iterators());
                }
            }
            

            out_file_stream.flush();
            out_file_stream.close();
        } else {
            std::cout << "ERROR: cannot write to file: " << out_file << "\n";
        }
    } catch (std::exception& e){
        std::cout << "CRITICAL: exception while write from file: " << out_file << e.what() << "\n";
    }
}

void Visited::read_paths(std::string in_file){
    try{
        std::ifstream input_file_stream (in_file);
        if (input_file_stream.is_open()){
            std::string one_line;
            std::getline (input_file_stream, one_line);
            int mapSize = std::stoi(one_line);
            input_tokens_map.clear();
            for (int i = 0; i < mapSize; i++){
                if (std::getline (input_file_stream, one_line)){
                    std::string key = one_line.substr(0, one_line.find(FILE_VALS_DELIMETER));
                    std::string value = one_line.substr(one_line.find(FILE_VALS_DELIMETER) + FILE_VALS_DELIMETER.size(), one_line.length());
                    input_tokens_map[key] = std::stoi(value);
                    //std::cout << "DEBUG: " << key << ":" << value << "\n";
                } else {
                    std::cout << "ERROR: reading map values from file: " << in_file << "\n";
                    return;
                }
            }
            // now reading tree
            while (std::getline (input_file_stream, one_line))
            {
                if (one_line.size() == 0){
                    end_txn();
                } else {
                    register_child(std::stoi(one_line));
                }
                //std::cout << "DEBUG: " << one_line << "\n";
            }
            input_file_stream.close();
        } else {
            std::cout << "ERROR: cannot read from file: " << in_file << "\n";
            return;
        }
    } catch (std::exception& e){
        std::cout << "CRITICAL: exception while read from file: " << in_file << e.what() << "\n";
    }
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