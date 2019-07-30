#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/common/thread.hpp>
#include <mysql/mysql.h>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <iostream>
#include <set>
#include <string>
#include <random>
#include <tuple>

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

    MYSQL* create_database_connection(){
       /*
       Function to create a mysql database connection 
       return: connection instance
       */
       MYSQL *conn;
       const char *server = "localhost";
       const char *user = "root";
       const char *password = "password";
       const char *database = "demo";
       conn = mysql_init(NULL);
       conn = mysql_real_connect(conn, server, user, password, database, 0, NULL, 0);
       return(conn);
    }

    MYSQL_RES* execute_query(MYSQL* conn, std::string query){
        /*
        Function to execute sql query and return result
        param: database connection instance - conn
        param: sql query - query
        return: result after executing query
        */
        MYSQL_RES *res;
        const char *q = query.c_str();
        int state = mysql_query(conn, q);
        res = mysql_use_result(conn);
        if(state==0){
          return(res);
        }
        else{
          std::cout<<"ERROR:"<<mysql_error(conn)<<std::endl;
        }
    }    

    std::string log_in(std::string username,std::string userpassword){
        /*
        Function to log in the user, validate if the user exists in the database, if so 
        generate a unique token else return an error as user not found
        param: username of user.
        param: password of user.
        return: Response json in string.
        */

        // create end point token and send it back
        MYSQL *conn;
        MYSQL_RES *res;
        MYSQL_ROW row;
        std::string status = "False";
        std::string query;

        conn = create_database_connection();
        query = "select * from user_account where username = '"+username+"' and password = '"+userpassword+"'";
        res = execute_query(conn, query);
        
        if(!mysql_fetch_row(res)){
            status = "False";
        }
        else{                                                                                               
            status = "True";
        }
        mysql_close(conn);

        std::string token = generate_random_string();
        std::string message = std::string("Welcome to Oracle.");
        
        std::vector <std::string> response_array;
        response_array.push_back("token"); 
        response_array.push_back(token); 
        response_array.push_back("message");
        response_array.push_back(message);
        response_array.push_back("status");
        response_array.push_back(status);
        response_array.push_back("action");
        response_array.push_back("log_in");
        
        std::string response_string = convert_vector_to_string_for_response(response_array);
        return response_string;
    }

    std::string user_list_in_json_format(){
        /*
        Function to convert user id and username of all users
        in json format
        returns string in the form :
        
        "supervisor_list":[
            {"user_id":"12", "username":"user0"},
            {"user_id":"1", "username":"user1"}
        ]
        */
        MYSQL_RES *res;
        MYSQL_ROW row;
        MYSQL *conn = create_database_connection();
        std::string query = "select user_id, username from user_account";
        std::string response_string;
        std::vector<std::tuple<std::string, std::string>> supervisor_user_list; 
       
        res = execute_query(conn, query);
        while((row = mysql_fetch_row(res))!=NULL){
            //vector of tuples of the form (user_id, username)
            supervisor_user_list.push_back(std::make_tuple(row[0], row[1]));
        }

        response_string += "\"supervisor_list\":[";
        for(int i=0; i<supervisor_user_list.size(); i++){
            response_string += "{\"user_id\":\""+std::get<0>(supervisor_user_list[i])+"\", \"username\":\""+std::get<1>(supervisor_user_list[i])+"\"}";
            if(i+1 < supervisor_user_list.size()){
                //add "," if there are more entries left
                response_string += ",";
            }
        }
        response_string += "]";
        
        return response_string;
    }

    std::string skill_set_in_json_format(){
        /*
        Function to convert skill id and skill name to json format
        returns string in the form :
        
        "user_skill_list":[
            {"skill_id":"0", "skill_name":"skill0"},
            {"skill_id":"1", "skill_name":"skill1"}
        ]
        */
        MYSQL_RES *res;
        MYSQL_ROW row;
        MYSQL *conn = create_database_connection();
        std::string query = "select skill_id, skill_name from work_skill";
        std::string response_string;
        std::vector<std::tuple<std::string, std::string>> user_skill_list; 
        
        res = execute_query(conn, query);     
        while((row = mysql_fetch_row(res))!=NULL){
            //vector of tuples of the form (user_id, username)
            user_skill_list.push_back(std::make_tuple(row[0], row[1]));
        }

        response_string += "\"user_skill_list\":[";
        for(int i=0; i<user_skill_list.size(); i++){
            response_string += "{\"skill_id\":\""+std::get<0>(user_skill_list[i])+"\", \"skill_name\":\""+std::get<1>(user_skill_list[i])+"\"}";
            if(i+1 < user_skill_list.size()){
                //add "," if there are more entries left
                response_string += ",";
            }
        }
        response_string += "]";
      
        return response_string;       
    }

    std::string get_user_creation_pop_up_details(){
        /*
        Function to create string in json format with details to
        be displayed in drop-down for supervisor and skills
        returns string in the form:
        {
            "action":"get_user_creation_pop_up_details",
            "supervisor_list":
            [
                {"user_id":"12", "username":"user0"},..
            ],
            "user_skill_list":
            [
                {"skill_id":"1", "skill_name":"skill1"},..
            ]
        }
        */
        std::string response_string;

        response_string += "{\"action\":\"get_user_creation_pop_up_details\",";
        response_string += user_list_in_json_format();
        response_string += ",";
        response_string += skill_set_in_json_format();
        response_string += "}";
        return response_string;
    }

    std::string create_user(std::string username, std::string firstname, std::string lastname, std::string userpassword, std::string supervisor_id, std::string user_start_date, std::string user_end_date, std::string user_status, std::string skill_id){
        /*
        Function to create a new user from values passed as 
        parameters
        return : json string with action and status
        {"action":"user_create", "status":"True"}
        
        */
        MYSQL *conn;
        MYSQL_RES *res;
        MYSQL_ROW row;
        std::string response = "";
        
        conn = create_database_connection();     
        std::string query = "insert into user_account(username, firstname, lastname, password, supervisor_id, user_start_date, user_end_date, user_status, skill_id) values ('"+username+"','"+firstname+"','"+lastname+"','"+userpassword+"','"+supervisor_id+"','"+user_start_date+"','"+user_end_date+"','"+user_status+"','"+skill_id+"')";
        res = execute_query(conn, query);
        if(res){
            response = "{\"action\":\"user_create\", \"status\":\"False\"}";
        }
        else{                                                                                               
            response = "{\"action\":\"user_create\", \"status\":\"True\"}";
        }
        mysql_close(conn);
        return response;
    }

    std::string edit_user(std::string user_id, std::string username, std::string firstname, std::string lastname, std::string userpassword, std::string supervisor_id, std::string user_start_date, std::string user_end_date, std::string user_status, std::string skill_id){
        MYSQL *conn;
        MYSQL_RES *res;
        MYSQL_ROW row;
        std::string response = "";
        
        conn = create_database_connection();     
        std::string query = "update user_account set username = \""+username+"\", firstname=\""+firstname+"\", lastname=\""+lastname+"\", password=\""+userpassword+"\", supervisor_id =\""+supervisor_id+"\", user_start_date = \""+user_end_date+"\", user_end_date = \""+user_end_date+"\", user_status = \""+user_status+"\", skill_id = \""+skill_id+"\" where user_id="+user_id;
        res = execute_query(conn, query);
        if(res){
            response = "{\"action\":\"user_edit\", \"status\":\"False\"}";
        }
        else{                                                                                               
            response = "{\"action\":\"user_edit\", \"status\":\"True\"}";
        }
        mysql_close(conn);
        return response;
    }

    std::string delete_user(std::string user_id){
        MYSQL *conn;
        MYSQL_RES *res;
        MYSQL_ROW row;
        std::string response = "";
        
        conn = create_database_connection();     
        std::string query = "delete from user_account where user_id="+user_id;
        res = execute_query(conn, query);
        if(res){
            response = "{\"action\":\"user_delete\", \"status\":\"False\"}";
        }
        else{                                                                                               
            response = "{\"action\":\"user_delete\", \"status\":\"True\"}";
        }
        mysql_close(conn);
        return response;
    }

    std::string list_user(){
        MYSQL_RES *res;
        MYSQL_ROW row;
        MYSQL *conn = create_database_connection();
        std::string query = "select user_id, username, firstname, lastname, password, supervisor_id, user_start_date, user_end_date, user_status, skill_id from user_account";
        std::string response_string="{\"action\":\"list_user\", \"users\":[";
        std::string user_id, username, firstname, lastname, supervisor_id, user_start_date, user_status, password, user_end_date, skill_id; 
       
        res = execute_query(conn, query);
        row = mysql_fetch_row(res);
        while(row !=NULL){
            user_id = row[0];
            username = row[1];
            firstname = row[2];
            lastname = row[3];
            password = row[4];
            supervisor_id = row[5];
            user_start_date = row[6];
            user_status = row[8];
            user_end_date = row[7];
            skill_id = row[9];
            response_string += "{\"user_id\":\""+user_id+
			"\",\"username\":\""+username+
			"\",\"firstname\":\""+firstname+
			"\",\"lastname\":\""+lastname+
            "\",\"password\":\""+password+
			"\",\"supervisor_id\":\""+supervisor_id+
			"\",\"user_start_date\":\""+user_start_date+
			"\",\"user_end_date\":\""+user_end_date+
			"\",\"user_status\":\""+user_status+
			"\",\"skill_id\":\""+skill_id+"\"}";
            if((row = mysql_fetch_row(res)) != NULL){
                response_string += ", ";
            }
            
        }
        response_string += "]}";
        
        return response_string;
    }

    std::string create_role(std::string role_name, std::string role_description, std::string role_start_date, std::string role_end_date){
        /*
        Function to create a new ROLE from values passed as 
        parameters
        return : json string with action and status
        {"action":"role_create", "status":"True"}
        
        */
        MYSQL *conn;
        MYSQL_RES *res;
        MYSQL_ROW row;
        std::string response = "";

        conn = create_database_connection(); 
        std::string query = "insert into roles(role_name, role_description, role_start_date, role_end_date) values ('"+role_name+"','"+role_description+"','"+role_start_date+"','"+role_end_date+"')";
        res = execute_query(conn, query);
        
        if(res){
            response = "{\"action\":\"role_create\", \"status\":\"False\"}";
        }
        else{                                                                                               
            response = "{\"action\":\"role_create\", \"status\":\"True\"}";
        }
        mysql_close(conn);
        return response;
    }

    std::string edit_role(std::string role_id, std::string role_name, std::string role_description, std::string role_start_date, std::string role_end_date){
        MYSQL *conn;
        MYSQL_RES *res;
        MYSQL_ROW row;
        std::string response = "";

        conn = create_database_connection(); 
        std::string query = "update roles set role_name = \""+role_name+"\", role_description=\""+role_description+"\", role_start_date=\""+role_start_date+"\", role_end_date=\""+role_end_date+"\" where role_id="+role_id;
        res = execute_query(conn, query);
        
        if(res){
            response = "{\"action\":\"role_edit\", \"status\":\"False\"}";
        }
        else{                                                                                               
            response = "{\"action\":\"role_edit\", \"status\":\"True\"}";
        }
        mysql_close(conn);
        return response;
    }

    std::string delete_role(std::string role_id){
        MYSQL *conn;
        MYSQL_RES *res;
        MYSQL_ROW row;
        std::string response = "";
        
        conn = create_database_connection();     
        std::string query = "delete from roles where role_id="+role_id;
        res = execute_query(conn, query);
        if(res){
            response = "{\"action\":\"role_delete\", \"status\":\"False\"}";
        }
        else{                                                                                               
            response = "{\"action\":\"role_delete\", \"status\":\"True\"}";
        }
        mysql_close(conn);
        return response;
    }

    std::string list_role(){
        MYSQL_RES *res;
        MYSQL_ROW row;
        MYSQL *conn = create_database_connection();
        std::string query = "select role_id, role_name, role_description, role_start_date, role_end_date from roles";
        std::string response_string="{\"action\":\"list_role\", \"roles\":[";
        std::string role_id, role_name, role_description, role_start_date, role_end_date; 
       
        res = execute_query(conn, query);
        row = mysql_fetch_row(res);
        while(row !=NULL){
            std::cout<<"\n\n\nhi\n\n\\n";
            role_id = row[0];
            role_name = row[1];
            role_description = row[2];
            role_start_date = row[3];
            role_end_date = row[4];
            response_string += "{\"role_id\":\""+role_id+
			"\",\"role_name\":\""+role_name+
			"\",\"role_description\":\""+role_description+
			"\",\"role_start_date\":\""+role_start_date+
            "\",\"role_end_date\":\""+role_end_date+"\"}";
            if((row = mysql_fetch_row(res)) != NULL){
                response_string += ", ";
            }
            
        }
        response_string += "]}";
        
        return response_string;
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
        action = "role_list";
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
            std::string password = std::string(parsed_response_json["password"].GetString());
            std::string supervisor_id = std::string(parsed_response_json["supervisor_id"].GetString());
            std::string user_start_date = std::string(parsed_response_json["user_start_date"].GetString());
            std::string user_end_date = std::string(parsed_response_json["user_end_date"].GetString());
            std::string user_status = std::string(parsed_response_json["user_status"].GetString());
            std::string skill_id = std::string(parsed_response_json["skill_id"].GetString());
            
            message = create_user(username, firstname, lastname, password, supervisor_id, user_start_date, user_end_date, user_status, skill_id);
        }

        else if(action == "user_edit"){
            std::string user_id = std::string(parsed_response_json["user_id"].GetString());
            std::string username = std::string(parsed_response_json["username"].GetString());
            std::string firstname = std::string(parsed_response_json["firstname"].GetString());
            std::string lastname = std::string(parsed_response_json["lastname"].GetString());
            std::string password = std::string(parsed_response_json["password"].GetString());
            std::string supervisor_id = std::string(parsed_response_json["supervisor_id"].GetString());
            std::string user_start_date = std::string(parsed_response_json["user_start_date"].GetString());
            std::string user_end_date = std::string(parsed_response_json["user_end_date"].GetString());
            std::string user_status = std::string(parsed_response_json["user_status"].GetString());
            std::string skill_id = std::string(parsed_response_json["skill_id"].GetString());
           
            message = edit_user(user_id, username, firstname, lastname, password, supervisor_id, user_start_date, user_end_date, user_status, skill_id);
        }
       
        else if(action == "user_delete"){
            std::string user_id = std::string(parsed_response_json["user_id"].GetString());
        
            message = delete_user(user_id);
        }

        else if(action == "user_list"){
            message = list_user();
        }

        else if(action == "role_create"){
            // Get the username, firstname, lastname, password, supervisor_id, user_status_id, skill_id
            std::string role_name = std::string(parsed_response_json["role_name"].GetString());
            std::string role_description = std::string(parsed_response_json["role_description"].GetString());
            std::string role_start_date = std::string(parsed_response_json["role_start_date"].GetString());
            std::string role_end_date = std::string(parsed_response_json["role_end_date"].GetString());

            message = create_role(role_name, role_description, role_start_date, role_end_date);
        }

        else if(action == "role_edit"){
            std::string role_id = std::string(parsed_response_json["role_id"].GetString());
            std::string role_name = std::string(parsed_response_json["role_name"].GetString());
            std::string role_description = std::string(parsed_response_json["role_description"].GetString());
            std::string role_start_date = std::string(parsed_response_json["role_start_date"].GetString());
            std::string role_end_date = std::string(parsed_response_json["role_end_date"].GetString());
           
            message = edit_role(role_id, role_name, role_description, role_start_date, role_end_date);
        }
    
        else if(action == "role_delete"){
            std::string role_id = std::string(parsed_response_json["role_id"].GetString());
        
            message = delete_role(role_id);
        }

        else if(action == "role_list"){
            message = list_role();
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
