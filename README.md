对应博文：https://blog.csdn.net/chenbb8/article/details/128134389

这是一个练手的工程，综合了esp32的几个example，写一个简单的透传demo。参考一下某透传模块的指令，提取出以下几条(==每条指令后应该加上换行符，比如"\n"、“\r\n”，否则将不识别==)：

- 扫描AP

指令 | ATWS
---|---
响应| AP : &lt;num>,&lt;ssid>,&lt;chl>,&lt;sec>,&lt;rssi>,&lt;bssid> <br>[ATWS] OK 

- 连接到 AP

指令 | ATPN=&lt;ssid>,&lt;pwd>
---|---
响应 | 成功 <br>[ATPN] OK <br>失败 <br>[ATPN] ERROR:<error_code>
error_code | 1: 命令格式错误<br>2: 参数错误<br>3: 连接 AP失败<br>4: dhcp 超时<br>5：无ap信息

- 建立socket

指令 | ATPC=&lt;mode>,&lt;Remote Addr>,&lt;Remote Port>
---|---
响应 | 成功 <br>[ATPC] OK <br>失败 <br>[ATPC] ERROR:<error_code>
参数 | mode: <br> &emsp;0:TCP
error_code | 1: 命令格式错误<br>2: 参数错误<br>3: 连接 server失败<br>4: 尚未连接AP
注意|连接成功后直接进入透传模式

## 环境

操作系统:ubuntu 20.04<br>
虚拟机：VMare Workstation 16<br>
IDE：vscode 1.73.1<br>
vscode插件：Espressif IDF v1.5.1<br>
board：淘宝上的ESP32-S3-DevKitC-1兼容板<br>
外置串口板子：淘宝上的cp2102 6合1串口模块<br>

## 硬件连接
esp32_TXD：IO4 <-> 串口模块:RXD<br>
esp32_RXD：IO5 <-> 串口模块:TXD<br>
