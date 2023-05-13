
acceptor 是个辅助类, 用于实现投递多个 accept 操作.

然后对接受的连接, 创建 client 对象. 并管理 client 对象.
在优雅退出的时候, 会逐个调用 client 对象的 close.

acceptor 不会直接创建 client 对象, 而是调用 Server 对象的 make_shared_connection 创建.
创建的对象首先被 传给 async_accept 操作. 因此创建的对象一开始是未连接状态.

等连接被接受, 则 调用 Server 对象的 client_connected 函数. 如果客户断开连接, 则会调用
Server 对象的 client_disconnected 函数, 以便 server 对象有办法清理绑定在 client 上的资源.

server 对象使用的时候, 应该根据 has_reuseport () 决定, 是为一个 socket 创建一个 acceptor
还是为每个 io_context 都创建一个 acceptor.

make_shared_connection 会传入 acceptor 的 executor 作为第一个参数. 因此如果 server 使用
acceptor per io_context 模型, 则直接用这个传入的 executor 创建 client 对象. 以便 acceptor
和旗下接受的 client 绑在同一个 io_context 上. _acceptor和client不在同一个
io_context 上会略微影响 async_accept 的性能._ 如果 server 使用的只是一个 acceptor, 则可以
使用 io_context_pool 里的 io_context 池选一个来创建 client 对象.
