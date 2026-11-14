link wokwi: https://wokwi.com/projects/480588233770261505
 
 🔥 FIREGUARD – HỆ THỐNG CẢNH BÁO CHÁY SỬ DỤNG ESP32

📌 Giới thiệu dự án

**FireGuard** là hệ thống cảnh báo cháy thông minh được xây dựng trên nền tảng vi điều khiển ESP32, nhằm giám sát các dấu hiệu nguy cơ cháy nổ thông qua cảm biến khí gas/khói MQ-2 và cảm biến nhiệt độ, độ ẩm DHT22.

Hệ thống sử dụng **FreeRTOS** để tổ chức và xử lý nhiều tác vụ song song, giúp việc đọc dữ liệu cảm biến, xử lý cảnh báo và truyền thông diễn ra độc lập, hiệu quả. Dữ liệu sau khi thu thập được gửi thông qua giao thức **MQTT** đến HiveMQ Broker, từ đó hiển thị trên giao diện Web Dashboard để người dùng theo dõi và điều khiển thiết bị từ xa.

Khi phát hiện các dấu hiệu bất thường, hệ thống có thể tự động kích hoạt còi báo động, quạt hút khói và máy bơm. Ngoài chế độ tự động, người dùng còn có thể chuyển sang chế độ thủ công để điều khiển thiết bị trực tiếp trên giao diện Web.

🎯 Mục tiêu dự án

- Giám sát nồng độ khí gas/khói và nhiệt độ môi trường theo thời gian thực.
- Phát hiện sớm các dấu hiệu có nguy cơ cháy nổ.
- Tự động kích hoạt các thiết bị cảnh báo và xử lý khi vượt ngưỡng cài đặt.
- Ứng dụng FreeRTOS để quản lý và thực hiện các tác vụ đồng thời.
- Sử dụng MQTT để trao đổi dữ liệu giữa ESP32 và Web Dashboard.
- Hỗ trợ giám sát và điều khiển hệ thống từ xa.

 🛠️ Công nghệ và linh kiện sử dụng

| Thành phần | Chức năng |
|---|---|
| ESP32 | Vi điều khiển trung tâm |
| MQ-2 | Phát hiện khí gas và khói |
| DHT22 | Đo nhiệt độ và độ ẩm |
| FreeRTOS | Quản lý các tác vụ đa nhiệm |
| MQTT – HiveMQ | Truyền nhận dữ liệu và lệnh điều khiển |
| HTML, CSS, JavaScript | Xây dựng giao diện Web Dashboard |
| Wokwi | Mô phỏng phần cứng và kiểm thử hệ thống |

⚙️ Chức năng chính

1. Đọc và xử lý dữ liệu từ cảm biến MQ-2 và DHT22.
2. Giám sát trạng thái môi trường và phát hiện nguy cơ cháy.
3. Điều khiển quạt hút khói, máy bơm và còi báo động.
4. Hỗ trợ hai chế độ hoạt động: AUTO và MANUAL.
5. Gửi dữ liệu cảm biến và trạng thái thiết bị lên Web Dashboard thông qua MQTT.
6. Cho phép người dùng theo dõi và điều khiển hệ thống từ xa.


 📂 Cấu trúc thư mục

- `ESP32/`: Mã nguồn điều khiển ESP32 và FreeRTOS.
- `Web/`: Mã nguồn giao diện Web Dashboard.
- `Wokwi/`: File cấu hình mô phỏng Wokwi.
- `Report/`: Báo cáo đồ án.

 👨‍💻 Thông tin dự án

- **Tên đề tài:** Xây dựng hệ thống cảnh báo cháy sử dụng ESP32 và MQTT.
- **Nền tảng mô phỏng:** Wokwi.
- **Vi điều khiển:** ESP32.
- **Giao thức truyền thông:** MQTT.
