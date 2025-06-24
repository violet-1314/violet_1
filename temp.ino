#include <dht11.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// DHT11 配置
dht11 DHT11;
#define DHT11PIN D4  // DHT11 数据引脚

// RGB LED 配置（共阴极，若为共阳极需调整引脚电平逻辑）
#define RGB_RED_PIN D5   
#define RGB_GREEN_PIN D6 
#define RGB_BLUE_PIN D7  

// 湿度控制普通 LED 配置
#define HUMIDITY_LED_PIN D1  
float humidityThreshold = 30.0;  // 湿度阈值，可远程调整

// 可控开关 LED 配置
#define CONTROL_LED_PIN D0  
bool controlLedState = false;    // 可控 LED 状态

// WiFi 配置
const char* ssid = "waerdeng";     
const char* password = "02090711"; 
WiFiServer server(80);            // 搭建 TCP 服务器
//onenet
const char* mqtt_server = "mqtts.heclouds.com";
const int mqtt_port = 1883;
const char* product_id = "817X0XSh39";
const char* device_id = "dht11";
const char* token = "version=2018-10-31&res=products%2F817X0XSh39%2Fdevices%2Fdht11&et=2063930479000&method=md5&sign=751mK9uaFxJWC3PodjwAwQ%3D%3D";

WiFiClient espClient;
PubSubClient client(espClient);

// 温度区间配置（可远程调整）
float lowTempThreshold = 20.0;   // 低温阈值
float highTempThreshold = 30.0;  // 高温阈值
float temperature;
float humidity;

// 函数声明
void reconnect();
double Fahrenheit(double celsius);
double Kelvin(double celsius);
double dewPoint(double celsius, double humidity);
double dewPointFast(double celsius, double humidity);
void setRGBLEDByTemperature(float temp);
void setupWiFi();
void handleClient();
void sendHeader(WiFiClient &client);
void sendHomePage(WiFiClient &client);
void sendRedirect(WiFiClient &client, const char* location);

void setup() {
  Serial.begin(115200);
  Serial.println("DHT11 + RGB LED + WiFi Control");
  Serial.print("LIBRARY VERSION: ");
  Serial.println(DHT11LIB_VERSION);

  // 引脚模式初始化
  pinMode(RGB_RED_PIN, OUTPUT);
  pinMode(RGB_GREEN_PIN, OUTPUT);
  pinMode(RGB_BLUE_PIN, OUTPUT);
  pinMode(HUMIDITY_LED_PIN, OUTPUT);
  pinMode(CONTROL_LED_PIN, OUTPUT);

  setupWiFi();  // 初始化 WiFi 连接
  client.setServer(mqtt_server, mqtt_port);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  
  // 处理 WiFi 客户端连接
  handleClient();  

  // 读取 DHT11 数据
  Serial.println("\n");
  int chk = DHT11.read(DHT11PIN);  
  Serial.print("Read sensor: ");
  switch (chk) {
    case DHTLIB_OK:
      Serial.println("OK");
      break;
    case DHTLIB_ERROR_CHECKSUM:
      Serial.println("Checksum error");
      break;
    case DHTLIB_ERROR_TIMEOUT:
      Serial.println("Time out error");
      break;
    default:
      Serial.println("Unknown error");
      break;
  }

  // 打印温湿度等数据
  Serial.print("Humidity (%): ");
  Serial.println((float)DHT11.humidity, 1);
  humidity=(float)DHT11.humidity;
  Serial.print("Temperature (oC): ");
  Serial.println((float)DHT11.temperature, 2);
  temperature=(float)DHT11.temperature;
  Serial.print("Temperature (oF): ");
  Serial.println(Fahrenheit(DHT11.temperature), 2);
  Serial.print("Kelvin: ");
  Serial.println(Kelvin(DHT11.temperature), 2);
  Serial.print("Dew Point (oC): ");
  Serial.println(dewPoint(DHT11.temperature, DHT11.humidity));
  Serial.print("Dew PointFast (oC): ");
  Serial.println(dewPointFast(DHT11.temperature, DHT11.humidity));

  // 控制 RGB LED 颜色
  setRGBLEDByTemperature(DHT11.temperature);

  // 控制湿度 LED
  if (DHT11.humidity < humidityThreshold) {
    digitalWrite(HUMIDITY_LED_PIN, HIGH);
  } else {
    digitalWrite(HUMIDITY_LED_PIN, LOW);
  }

  // 控制可控开关 LED
  digitalWrite(CONTROL_LED_PIN, controlLedState ? HIGH : LOW);

  // 构建JSON数据
  String payload = "{\"id\":\"123\",\"params\":{\"temp\":{\"value\":" + String(temperature) +  "},\"humidity\":{\"value\":" + String(humidity) + "}}}";
  // 发送数据
  String topic = "$sys/" + String(product_id) + "/" + String(device_id) + "/thing/property/post";
  client.publish(topic.c_str(), payload.c_str());
  Serial.println("Data published: " + payload);

  delay(2000);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(device_id, product_id, token)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// 温度转华氏度
double Fahrenheit(double celsius) {
  return 1.8 * celsius + 32;
}

// 温度转开尔文
double Kelvin(double celsius) {
  return celsius + 273.15;
}

// 计算露点
double dewPoint(double celsius, double humidity) {
  double A0 = 373.15 / (273.15 + celsius);
  double SUM = -7.90298 * log10(A0);
  SUM += 5.02808 * log10(A0);
  SUM += -1.3816e-7 * (pow(10, (11.344 * (1 - 1 / A0))) - 1);
  SUM += 8.1328e-3 * (pow(10, (-3.49149 * (A0 - 1))) - 1);
  SUM += log10(1013.246);
  double VP = pow(10, SUM - 3) * humidity;
  double T = (log10(VP / 0.61078));  
  return (241.88 * T) / (17.558 - T);
}

// 快速计算露点
double dewPointFast(double celsius, double humidity) {
  double a = 17.271;
  double b = 237.7;
  double temp = (a * celsius) / (b + celsius) + log(humidity / 100);
  double Td = (b * temp) / (a - temp);
  return Td;
}

// 根据温度设置 RGB LED 颜色
void setRGBLEDByTemperature(float temp) {
  if (temp < lowTempThreshold) {
    // 低温：蓝色
    digitalWrite(RGB_RED_PIN, LOW);
    digitalWrite(RGB_GREEN_PIN, LOW);
    digitalWrite(RGB_BLUE_PIN, HIGH);
  } else if (temp > highTempThreshold) {
    // 高温：红色
    digitalWrite(RGB_RED_PIN, HIGH);
    digitalWrite(RGB_GREEN_PIN, LOW);
    digitalWrite(RGB_BLUE_PIN, LOW);
  } else {
    // 正常温度：绿色
    digitalWrite(RGB_RED_PIN, LOW);
    digitalWrite(RGB_GREEN_PIN, HIGH);
    digitalWrite(RGB_BLUE_PIN, LOW);
  }
}

// 初始化 WiFi 并启动服务器
void setupWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  server.begin();  // 启动 TCP 服务器
  Serial.println("Server started");
}

// 处理 HTTP 响应头
void sendHeader(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Cache-Control: no-cache, no-store, must-revalidate");
  client.println("Pragma: no-cache");
  client.println("Expires: 0");
  client.println();
}

// 发送重定向响应
void sendRedirect(WiFiClient &client, const char* location) {
  client.println("HTTP/1.1 303 See Other");
  client.print("Location: ");
  client.println(location);
  client.println("Cache-Control: no-cache, no-store, must-revalidate");
  client.println("Pragma: no-cache");
  client.println("Expires: 0");
  client.println();
}

// 生成并发送首页 HTML
void sendHomePage(WiFiClient &client) {
  sendHeader(client);
  
  client.println("<html><body>");
  client.println("<h1>DHT11 + LED Control</h1>");
  client.println("<p>Current Temp Thresholds: Low=" + String(lowTempThreshold) + "℃, High=" + String(highTempThreshold) + "℃</p>");
  client.println("<p>Current Humidity Threshold: " + String(humidityThreshold) + "%</p>");
  client.println("<form action=\"/setTempThresholds\">");
  client.println("Set Low Temp: <input type=\"number\" name=\"low\" value=\"" + String(lowTempThreshold) + "\"><br>");
  client.println("Set High Temp: <input type=\"number\" name=\"high\" value=\"" + String(highTempThreshold) + "\"><br>");
  client.println("<input type=\"submit\" value=\"Update Temp Thresholds\">");
  client.println("</form><br>");

  client.println("<form action=\"/setHumidityThreshold\">");
  client.println("Set Humidity Threshold: <input type=\"number\" name=\"val\" value=\"" + String(humidityThreshold) + "\"><br>");
  client.println("<input type=\"submit\" value=\"Update Humidity Threshold\">");
  client.println("</form><br>");

  client.println("<form action=\"/controlLed\">");
  client.println("Control LED State: <select name=\"state\">");
  
  // 使用 String 对象拼接 HTML
  String option1 = "<option value=\"1\" ";
  if (controlLedState) {
    option1 += "selected";
  }
  option1 += ">On</option>";
  client.println(option1);
  
  String option2 = "<option value=\"0\" ";
  if (!controlLedState) {
    option2 += "selected";
  }
  option2 += ">Off</option>";
  client.println(option2);
  
  client.println("</select><br>");
  client.println("<input type=\"submit\" value=\"Update LED State\">");
  client.println("</form>");
  client.println("</body></html>");
}

// 处理 WiFi 客户端请求
void handleClient() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  Serial.println("New client connected");
  bool responseSent = false;
  
  while (client.connected() && !responseSent) {
    if (client.available()) {
      String request = client.readStringUntil('\n');
      Serial.println("Request: " + request);
      
      // 处理温度阈值设置请求
      if (request.indexOf("/setTempThresholds?") != -1) {
        int lowStart = request.indexOf("low=") + 4;
        int lowEnd = request.indexOf("&", lowStart);
        int highStart = request.indexOf("high=") + 5;
        
        if (lowStart > 4 && lowEnd > lowStart && highStart > 5) {
          lowTempThreshold = request.substring(lowStart, lowEnd).toFloat();
          highTempThreshold = request.substring(highStart, request.indexOf(" ", highStart)).toFloat();
          Serial.print("New temp thresholds: low=");
          Serial.print(lowTempThreshold);
          Serial.print(", high=");
          Serial.println(highTempThreshold);
        }
        
        sendRedirect(client, "/");
        responseSent = true;
      }
      // 处理湿度阈值设置请求
      else if (request.indexOf("/setHumidityThreshold?") != -1) {
        int valStart = request.indexOf("val=") + 4;
        
        if (valStart > 4) {
          humidityThreshold = request.substring(valStart, request.indexOf(" ", valStart)).toFloat();
          Serial.print("New humidity threshold: ");
          Serial.println(humidityThreshold);
        }
        
        sendRedirect(client, "/");
        responseSent = true;
      }
      // 处理LED控制请求
      else if (request.indexOf("/controlLed?") != -1) {
        int stateStart = request.indexOf("state=") + 6;
        
        if (stateStart > 6) {
          controlLedState = (request.substring(stateStart, request.indexOf(" ", stateStart)).toInt() == 1);
          Serial.print("Control LED state: ");
          Serial.println(controlLedState ? "ON" : "OFF");
        }
        
        sendRedirect(client, "/");
        responseSent = true;
      }
      // 处理首页请求
      else {
        sendHomePage(client);
        responseSent = true;
      }
      
      // 读取并忽略剩余的 HTTP 请求头
      while (client.available() && client.readStringUntil('\n') != "\r") {
        // 忽略其他行
      }
    }
  }
  
  // 关闭客户端连接
  delay(1);
  client.stop();
  Serial.println("Client disconnected");
}
