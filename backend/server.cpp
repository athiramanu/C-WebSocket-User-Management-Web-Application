#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/common/thread.hpp>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <iostream>
#include <set>
#include <string>
#include <random>




typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

using websocketpp::lib::thread;
using websocketpp::lib::mutex;
using websocketpp::lib::lock_guard;
using websocketpp::lib::unique_lock;
using websocketpp::lib::condition_variable;

using namespace rapidjson;


/* on_open insert connection_hdl into channel
 * on_close remove connection_hdl from channel
 * on_message queue send to all channels
 */

enum action_type {
    SUBSCRIBE,
    UNSUBSCRIBE,
    MESSAGE
};

struct action {
    action(action_type t, connection_hdl h) : type(t), hdl(h) {}
    action(action_type t, connection_hdl h, server::message_ptr m)
      : type(t), hdl(h), msg(m) {}

    action_type type;
    websocketpp::connection_hdl hdl;
    server::message_ptr msg;
};

class broadcast_server {
public:
    broadcast_server() {
        // Initialize Asio Transport
        m_server.init_asio();

        // Register handler callbacks
        m_server.set_open_handler(bind(&broadcast_server::on_open,this,::_1));
        m_server.set_close_handler(bind(&broadcast_server::on_close,this,::_1));
        m_server.set_message_handler(bind(&broadcast_server::on_message,this,::_1,::_2));
    }

    void run(uint16_t port) {
        // listen on specified port
        m_server.listen(port);

        // Start the server accept loop
        m_server.start_accept();

        // Start the ASIO io_service run loop
        try {
            m_server.run();
        } catch (const std::exception & e) {
            std::cout << e.what() << std::endl;
        }
    }

    void on_open(connection_hdl hdl) {
        {
            lock_guard<mutex> guard(m_action_lock);
            m_actions.push(action(SUBSCRIBE,hdl));
        }
        m_action_cond.notify_one();
    }

    void on_close(connection_hdl hdl) {
        {
            lock_guard<mutex> guard(m_action_lock);
            m_actions.push(action(UNSUBSCRIBE,hdl));
        }
        m_action_cond.notify_one();
    }

    void on_message(connection_hdl hdl, server::message_ptr msg) {
        // queue message up for sending by processing thread
        {
            lock_guard<mutex> guard(m_action_lock);
            m_actions.push(action(MESSAGE,hdl,msg));
        }
        m_action_cond.notify_one();
    }

    void process_messages() {
        
        while(1) {
            unique_lock<mutex> lock(m_action_lock);
            while(m_actions.empty()) {
                m_action_cond.wait(lock);
            }

            action a = m_actions.front();
            m_actions.pop();

            lock.unlock();

            if (a.type == SUBSCRIBE) {
                lock_guard<mutex> guard(m_connection_lock);
                m_connections.insert(a.hdl);
            } else if (a.type == UNSUBSCRIBE) {
                lock_guard<mutex> guard(m_connection_lock);
                m_connections.erase(a.hdl);
            } else if (a.type == MESSAGE) {
                lock_guard<mutex> guard(m_connection_lock);

                con_list::iterator it;
                for (it = m_connections.begin(); it != m_connections.end(); ++it) {
                    
                    // Parse json from the response and the get the action
                    std::string response_json = a.msg->get_payload();
                    char char_response_json[response_json.length()]; 

                    for (int i = 0; i < sizeof(char_response_json); i++) { 
                        char_response_json[i] = response_json[i]; 
                    } 
                    Document parsed_response_json = parse_json(response_json.c_str());

                    std::string payload_response = compare_and_perform_action(parsed_response_json);
                    
                    a.msg->set_payload(payload_response);
                    m_server.send(*it,a.msg);
                }
            } else {
                // undefined.
            }
        }
    }

    std::string convert_vector_to_string_for_response(std::vector <std::string> response_array){
        /*
        Function to convert a vector of strings to a json string

        param: Vector of string
        return: String in the form of json
        */
        std::string response_string = "{";
        for (int i=0; i<response_array.size(); i++) { 
            if(i%2==0){
                response_string += "\"" + response_array[i] + "\":"; 
            }
            else{
                if(i!=response_array.size()-1){
                    response_string += "\"" + response_array[i] + "\",";
                }
                else{
                    response_string += "\"" + response_array[i] + "\"";
                }
            }
        } 
        response_string += "}";
        return response_string;
    }

    std::string log_in(std::string username,std::string password){
        /*
        Function to log in the user, validate if the user exists in the database, if so 
        generate a unique token else return an error as user not found

        param: username of user.
        param: password of user.
        return: Response json in string.
        */

        // verify record in database 
        // TODO: Query in the database

        // create end point token and send it back
        std::string token = generate_random_string();
        std::string message = std::string("Welcome to Oracle.");
        
        std::vector <std::string> response_array;
        response_array.push_back("token"); 
        response_array.push_back(token); 
        response_array.push_back("message");
        response_array.push_back(message);
        response_array.push_back("status");
        response_array.push_back("True");
        response_array.push_back("action");
        response_array.push_back("log_in");
        
        std::string response_string = convert_vector_to_string_for_response(response_array);
        return response_string;
    }

    std::string get_user_creation_pop_up_details(){
        // Get the list of superviors
        // TODO: Query to database and list the username along with id , remove dummy data below after
        std::vector<std::tuple<std::string, int> > dummy_supervisor_user_list; 
        dummy_supervisor_user_list.push_back(std::make_tuple("Arvind", 1)); 
        dummy_supervisor_user_list.push_back(std::make_tuple("Arun", 2)); 
        dummy_supervisor_user_list.push_back(std::make_tuple("Athira", 3)); 

        //Get the list of user_status 
        // TODO: Query to database and list the user status along with id, remove dummy data below after   
        std::vector<std::tuple<std::string, int> > dummy_user_status_list; 
        dummy_user_status_list.push_back(std::make_tuple("active", 1)); 
        dummy_user_status_list.push_back(std::make_tuple("inactive", 2)); 

        return std::string("{\"action\":\"get_user_creation_pop_up_details\"}");
    }

    std::string compare_and_perform_action(const rapidjson::Document& parsed_response_json){
        /*
        Function to compare the incoming action and perform this action along with 
        the parsed response data passed.

        param parsed_response_json: Document object which has the response ( in json format )
        return:
        */
        std::string action = parsed_response_json["action"].GetString();
        std::string message = "";
        std::cout<<"action"<<action;
        if(action == "log_in"){
            // Get username and get password 
            std::string username = std::string(parsed_response_json["username"].GetString());
            std::string password = std::string(parsed_response_json["password"].GetString());

            message = log_in(username,password);        
        }
        else if(action == "user_create"){
            // Get the username, firstname, lastname, password, supervisor_id, user_status_id, skill_id
            std::string username = std::string(parsed_response_json["username"].GetString());
            std::string firstname = std::string(parsed_response_json["firstname"].GetString());
            std::string lastname = std::string(parsed_response_json["lastname"].GetString());
            std::string is_supervisor = std::string(parsed_response_json["is_supervisor"].GetString());
            std::string supervisor_id = std::string(parsed_response_json["supervisor_id"].GetString());
            std::string user_status_id = std::string(parsed_response_json["user_status_id"].GetString());
            std::string skill_id = std::string(parsed_response_json["skill_id"].GetString());
        }
        else if(action == "get_user_creation_pop_up_details"){
            // Call the function to get neccessary information to populate drop downs.
            message = get_user_creation_pop_up_details();
        }
        return message;

    }


    std::string generate_random_string()
    {
        /*
        Function to generate a random token for user to call API endpoints using these
        tokens for authentication purposes

        return: A token string of size 64
        */
        std::string str("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");

        std::random_device rd;
        std::mt19937 generator(rd());

        std::shuffle(str.begin(), str.end(), generator);

        return str.substr(0, 64);    // assumes 32 < number of characters in str         
    }

    

    Document parse_json(const char * json_string){
        /*
        Function to parse the json string to a json object - Document object

        param json_string : JSON in string format that needs to be parsed into a json
        return: Returns a Document object - parsed json.
        */
        // std::string charStr = string(1, *json_string);
        Document parsed_json;
        parsed_json.Parse(json_string);
        return parsed_json;
    }
    private:typedef std::set<connection_hdl,std::owner_less<connection_hdl> > con_list;

    server m_server;
    con_list m_connections;
    std::queue<action> m_actions;

    mutex m_action_lock;
    mutex m_connection_lock;
    condition_variable m_action_cond;
};

int main() {
    try {
    broadcast_server server_instance;

    // Start a thread to run the processing loop
    thread t(bind(&broadcast_server::process_messages,&server_instance));

    // Run the asio loop with the main thread
    server_instance.run(9002);

    t.join();

    } catch (websocketpp::exception const & e) {
        std::cout << e.what() << std::endl;
    }
}
