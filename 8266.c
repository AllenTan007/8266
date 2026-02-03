#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
// ================= 用户配置区 (Hardcoded Config) =================
const char* target_ssid     = "1";          // ⬅️ 硬编码热点名称
const char* target_password = "88888888";   // ⬅️ 硬编码热点密码
// ===========================================================
// --- 硬件定义 ---
const int PIN_SENSOR = 13;   // 光敏/激光接收管 (D7)
const int PIN_WS2812 = 12;   // WS2812B 数据脚 (D6)
const int NUM_LEDS   = 3;    // 灯珠数量
const int TCP_PORT   = 8080; // 手机端端口
const int UDP_PORT   = 8266; // 本机UDP端口
Adafruit_NeoPixel strip(NUM_LEDS, PIN_WS2812, NEO_GRB + NEO_KHZ800);
WiFiUDP udp;
WiFiClient client;
IPAddress serverIP;
bool hasFoundServer = false;
bool isTargetActive = false; 
unsigned long activeStartTime = 0;
int activeDuration = 3000;   
void setup() {
  Serial.begin(115200);
  
  // LED 初始化
  strip.begin();
  strip.setBrightness(50); // User requested 50 brightness in original code
  strip.show(); 
  
  pinMode(PIN_SENSOR, INPUT_PULLUP);
  
  Serial.println("\n[系统启动 - Hardcoded Hotspot + NeoPixel (Original Colors)]...");
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  
  // ================= WiFi 连接逻辑 (Clean/No Save) =================
  WiFi.persistent(false); // 彻底禁止保存
  WiFi.disconnect(true);
  
  Serial.println("[WiFi] 忽略旧配置，尝试连接默认热点 '1'...");
  WiFi.begin(target_ssid, target_password);
  
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < 15) {
      delay(500); Serial.print("1"); retryCount++;
      static bool s = false; s=!s;
      setAllLeds(s ? strip.Color(255, 0, 0) : 0); // Red Flash
  }

  if (WiFi.status() != WL_CONNECTED) {
     Serial.println("\n[WiFi] 默认热点不可用，启动 SmartConfig 配网模式...");
     WiFi.beginSmartConfig();
     
     // Loop forever waiting for SmartConfig
     while (!WiFi.smartConfigDone()) {
         // Flash Purple/Yellow to indicate Config Mode
         static bool s = false; s=!s;
         setAllLeds(s ? strip.Color(255, 0, 0) : 0); // Red Flash (User: Not connected = Red) 
         delay(500);
         Serial.print("SC");
         
         // If connection happens during SC
         if (WiFi.status() == WL_CONNECTED) {
             WiFi.stopSmartConfig();
             break;
         }
     }
     Serial.println("\n[WiFi] SmartConfig 接收成功!");
  }
  // ====================================================
  Serial.println("\n[WiFi] 成功连接: " + WiFi.localIP().toString());
  
  // 成功连接：蓝灯闪烁 (User Original: Blue flash)
  setAllLeds(strip.Color(0, 0, 255));
  delay(500);
  setAllLeds(0);
  
  udp.begin(UDP_PORT);
  Serial.println("[UDP] 服务已就绪");
}
// 统一控制颜色
void setAllLeds(uint32_t color) {
  for(int i=0; i<NUM_LEDS; i++) strip.setPixelColor(i, color);
  strip.show();
}
void findServer() {
  static unsigned long lastUDP = 0;
  if (millis() - lastUDP > 1000) {
    lastUDP = millis();
    
    IPAddress broadcastIP = WiFi.broadcastIP();
    udp.beginPacket(broadcastIP, UDP_PORT);
    udp.write("WHO_IS_SERVER");
    udp.endPacket();
    Serial.println("[UDP] 呼叫手机...");
  }
  
  int packetSize = udp.parsePacket();
  if (packetSize) {
    String packetMsg = "";
    while(udp.available()) packetMsg += (char)udp.read();
    
    if(packetMsg.length() > 0) {
        serverIP = udp.remoteIP();
        hasFoundServer = true;
        Serial.print("[UDP] 找到手机IP: "); Serial.println(serverIP);
        
        // 找到服务器：绿灯短闪 (User Original: 0, 100, 0)
        setAllLeds(strip.Color(0, 100, 0));
        delay(200);
        setAllLeds(0);
    }
  }
}
void connectToApp() {
  Serial.print("[TCP] 连接 App: "); Serial.println(serverIP);
  
  if (client.connect(serverIP, TCP_PORT)) {
    Serial.println("[TCP] 已连接!");
    
    client.setNoDelay(true);
    client.println("ID:" + WiFi.macAddress() + "|STATUS:READY");
    
    // 连接成功：绿灯长亮 (User Original: 0, 255, 0)
    setAllLeds(strip.Color(0, 255, 0)); 
    delay(1000);
    setAllLeds(0);
  } else {
    Serial.println("[TCP] 失败，重试...");
    delay(2000);
  }
}
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] 掉线重连中...");
    // 掉线/重连时：红灯闪烁 (Fix: Ensure Red Flash instead of stale state)
    static bool s = false; s=!s;
    setAllLeds(s ? strip.Color(255, 0, 0) : 0); 
    delay(500);
    return; 
  }
  
  if (!hasFoundServer) {
    findServer();
  } else if (!client.connected()) {
    // 掉线(TCP断开)：蓝灯 (User Original)
    setAllLeds(strip.Color(0, 0, 255)); 
    hasFoundServer = false;
    connectToApp();
  } else {
    processCommands();
    checkHit();
    checkTimeout();
  }
}

void processCommands() {
  while (client.available()) {
    String line = client.readStringUntil('\n'); 
    line.trim();
    if (line.length() == 0) continue;
    Serial.println("[RX] " + line);
    
    if (line.startsWith("CMD:ON")) {
        int timeIdx = line.indexOf("TIME:");
        if (timeIdx > 0) {
            String timeStr = line.substring(timeIdx + 5);
            activeDuration = timeStr.toInt();
        } else {
            activeDuration = 3500;
        }
        
        isTargetActive = true;
        activeStartTime = millis();
        // 激活状态：白光 (User Original)
        setAllLeds(strip.Color(255, 255, 255)); 
        Serial.println("任务开启");
        
    } else if (line.startsWith("CMD:OFF")) {
        isTargetActive = false;
        setAllLeds(0);
        
    } else if (line.startsWith("CMD:IDENTIFY")) {
        // 点名功能：紫灯闪烁 3 次
        Serial.println("收到点名指令 (IDENTIFY)");
        for(int i=0; i<3; i++) {
           setAllLeds(strip.Color(255, 0, 255)); // Purple
           delay(300);
           setAllLeds(0);
           delay(300);
        }
        // Restore state (Off or Active? Usually Off currently)
        if (isTargetActive) {
            setAllLeds(strip.Color(255, 255, 255));
        } else {
            setAllLeds(0);
        }
    }
  }
}
void checkTimeout() {
  if (isTargetActive && (millis() - activeStartTime > (unsigned long)activeDuration)) {
    isTargetActive = false;
    setAllLeds(0);
    Serial.println("超时灭灯");
  }
}
void checkHit() {
  if (isTargetActive) {
    if (digitalRead(PIN_SENSOR) == LOW) {
       Serial.println("[HIT] 击中!");
       
       isTargetActive = false;
       setAllLeds(0); // 击中灭灯 (User Original)
       
       client.print("RESP:HIT|MAC:" + WiFi.macAddress() + "\n");
       
       delay(150); 
    }
  }
}