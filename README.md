这是一个练手的工程，综合了esp32的几个example，写一个简单的透传demo。参考一下某透传模块的指令，提取出以下几条：

- 扫描AP

指令 | ATWS
---|---
响应| AP : <num>,<ssid>,<chl>,<sec>,<rssi>,<bssid> <br>[ATWS] OK 

- 连接到 AP

指令 | ATPN=<ssid>,<pwd>
---|---
响应 | 成功 <br>[ATPN] OK <br>失败 <br>[ATPN] ERROR:<error_code>
error_code | 1: 命令格式错误<br>2: 参数错误<br>3: 连接 AP错误<br>4: dhcp 超时

- 建立socket

指令 | ATPC=<mode>,< Remote Addr>,< Remote Port>
---|---
响应 | 成功 <br>[ATPC] OK <br>失败 <br>[ATPC]:<error_code>
参数 | mode: <br> &emsp;0:TCP
error_code | 1: 命令格式错误<br>2: 参数错误<br>3: 连接 server错误<br>4: 尚未连接AP

- 设置透传模式

指令 | ATPU
---|---
响应 | 成功 <br>[ATPU] OK <br>失败 <br>[ATPU] ERROR:<error_code>
error_code | 1: 命令格式错误<br>2: 尚未连server

## 环境

操作系统:ubuntu 20.04<br>
虚拟机：VMare Workstation 16<br>
IDE：vscode 1.73.1<br>
vscode插件：Espressif IDF v1.5.1<br>
board：淘宝上的ESP32-S3-DevKitC-1兼容板<br>
外置串口板子：淘宝上的cp2102 6合1串口模块<br>