$( document ).ready(function() {

    // Hide all other pages
    // TODO: Find a better solution to hide these pages
    $("#registrationPage").hide();
   
    window.WebSocket = window.WebSocket || window.MozWebSocket;
    var websocket = new WebSocket('ws://127.0.0.1:9002');

    // Constants to call the Web socket End Point
    var get_user_creation_pop_details = {
      "action": "get_user_creation_pop_up_details",
    }
    var login_details = {
      "action": "log_in",
      
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
          console.log(status);
          if(status=="True"){
            perform_log_in();
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

    var init_registration_page = function(){
      // Get the registration page details
      websocket.send(JSON.stringify(get_user_creation_pop_details));
    }

    var perform_log_in = function(){
      $('#logInPage').hide();
      $("#registrationPage").show();

      init_registration_page();
    }
});
