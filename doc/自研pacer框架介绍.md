system_timestamp：pacer的时间戳类，秒，毫秒，纳秒，微妙的获取及设置

send_packet_to_queue：把封装后的网络数据包入队到队列，调用pacer_packet_queue的类方法

send_packet_to_network：从队列中取出数据发送到网络中

pacer_packet_queue：pacer队列相关的类，直接操作队列

media_packet：对外来网络数据包的封装类

delay_task：定时器相关的类，什么时候醒来，睡，醒，再睡；它不知道速率，债务，包大小。

pacer_controller : 主控，计算债务，速率，计算何时发包，对外查询，等检查。

