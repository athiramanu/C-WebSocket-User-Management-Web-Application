$( document ).ready(function() {

    // Hide all other pages
    // TODO: Find a better solution to hide these pages
    $("#dashboard").hide();
    $("#newUser").hide();
    $("#loginError").hide();
    $('#newRoleForm').hide();

    window.WebSocket = window.WebSocket || window.MozWebSocket;
    var websocket = new WebSocket('ws://127.0.0.1:9002');

    // Constants to call the Web socket End Point
    var get_user_creation_pop_up_details = {
      "action": "get_user_creation_pop_up_details",
    }
    var login_details = {
      "action": "log_in",
      
    }
    var user_create = {
      "action": "user_create",
    }
    var role_create = {
      "action": "role_create",
    }
    websocket.onopen = function () {
      //pass
    };
    websocket.onerror = function () {
     // do something when websocket error occurs
    };
    websocket.onmessage = function (message) {
      // Funtion that is called when websocket message is sent, the reply 
      var response = JSON.parse(message.data);
      
      if(response["action"] == "log_in"){
          // Check if status is True 
          var status = response["status"];
          if(status=="True"){
            perform_log_in();
          }
          else{
            $("#loginError").show();
          }
      }
      
      else if(response["action"] == "get_user_creation_pop_up_details"){
        //Populate drop down for supervisor list
        var supervisorDropdown = document.getElementById('supervisor');
        for (var i = 0; i < response["supervisor_list"].length; i++) {
            supervisorDropdown.innerHTML = supervisorDropdown.innerHTML +
                '<option value="' + response["supervisor_list"][i]['user_id'] + '">' + response["supervisor_list"][i]['username'] + '</option>';
        }
        //Populate drop down for skill set
        var skillDropdown = document.getElementById('skill_id');
        for (var i = 0; i < response["user_skill_list"].length; i++) {
            skillDropdown.innerHTML = skillDropdown.innerHTML +
                '<option value="' + response["user_skill_list"][i]['skill_id'] + '">' + response["user_skill_list"][i]['skill_name'] + '</option>';
        }
      }

      else if(response["action"] == "user_create"){
          var status = response["status"];
          console.log(status);
          if(status=="True"){
            alert("New user created");
          }
          else{
            alert("Something went wrong");
          }
      }

      else if(response["action"] == "role_create"){
          var status = response["status"];
          console.log(status);
          if(status=="True"){
            alert("New role created");
          }
          else{
            alert("Something went wrong");
          }
      }
      else{
        console.log(response);
      }
    };

    $('#logInButton').click(function(e) {
      e.preventDefault();
      login_details["username"] = $("#username").val(),
      login_details["password"]=  $("#password").val()
      console.log(JSON.stringify(login_details));
      websocket.send(JSON.stringify(login_details));
      
    });

    $('#createUser').click(function(e){
      e.preventDefault();
      $("#newUser").show();
      $('#newRoleForm').hide();
    });

    $('#createRole').click(function(e){
      e.preventDefault();
      $("#newUser").hide();
      $('#newRoleForm').show();
    });

    $('#addUser').click(function(e){
        e.preventDefault();
        user_create["username"] = $("#newUsername").val()
        user_create["firstname"] = $("#newFirstName").val();
        user_create["lastname"] = $("#newLastName").val();
        user_create["password"] = $("#newPassword").val();
        user_create["supervisor_id"] = $("#supervisor").val();
        user_create["user_start_date"] = $("#newStartDate").val();
        user_create["user_end_date"] = $("#newEndDate").val();
        user_create["user_status"] = "active";
        user_create["skill_id"] = $("#skill_id").val();  
        console.log(JSON.stringify(user_create));
        websocket.send(JSON.stringify(user_create));
      });

    $('#addRole').click(function(e){
        e.preventDefault();
        role_create["role_name"] = $("#newRoleName").val()
        role_create["role_description"] = $("#newRoleDescription").val();
        role_create["role_start_date"] = $("#newRoleStartDate").val();
        role_create["role_end_date"] = $("#newRoleEndDate").val()  
        console.log(JSON.stringify(role_create));
        websocket.send(JSON.stringify(role_create));
      });

    var init_registration_page = function(){
      // Get the registration page details
      websocket.send(JSON.stringify(get_user_creation_pop_up_details));
    }

    var perform_log_in = function(){
      $('#logInPage').hide();
      $('#dashboard').show();
      init_registration_page();
    }
});
