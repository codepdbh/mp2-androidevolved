// Winsock event-based overlapped receives. Guest state and this queue are
// protected by MachineMutex; readiness is pumped without blocking the renderer.
#include <sys/uio.h>
namespace net {
struct Receive {
    uint32_t socket,overlapped,flags,flagsOut,address,addressSize,event;
    std::vector<std::pair<uint32_t,uint32_t>> buffers;
};
std::map<uint32_t,Receive> receives;
std::map<uint32_t,uint32_t> completedFlags;
void signal(uint32_t event){auto found=eventHandles.find(event&~1u);if(found!=eventHandles.end())found->second->signaled=true;}
void complete(const Receive& op,uint32_t error,uint32_t bytes,uint32_t flags){
    write32(op.overlapped,error);write32(op.overlapped+4,bytes);
    completedFlags[op.overlapped]=flags;
    if(op.flagsOut)write32(op.flagsOut,flags);
    signal(op.event);
}
// False only for would-block. An actual failure completes the operation too.
bool receive(Receive& op,uint32_t& bytes,uint32_t& flags,uint32_t& error){
    auto* socket=find(op.socket);if(!socket){error=995;return true;}
    std::vector<iovec> buffers;buffers.reserve(op.buffers.size());
    for(auto item:op.buffers)buffers.push_back({item.second?ptr(item.first,item.second):nullptr,item.second});
    sockaddr_storage peer{};msghdr message{};
    message.msg_iov=buffers.data();message.msg_iovlen=buffers.size();
    message.msg_name=&peer;message.msg_namelen=sizeof peer;
    int nativeFlags=int(op.flags&3);if(op.flags&8)nativeFlags|=MSG_WAITALL;
    ssize_t received=::recvmsg(socket->fd,&message,nativeFlags);
    if(received<0){if(errno==EAGAIN||errno==EWOULDBLOCK)return false;error=wsaError(errno);return true;}
    bytes=uint32_t(received);flags=message.msg_flags&MSG_OOB?1u:0u;
    if(peer.ss_family==AF_INET){
        auto address=describe(peer);
        notePeer(*socket,"recibe (overlapped) de",address,received);
        count(op.socket,*socket,address,false,received);
    }
    if(message.msg_flags&MSG_TRUNC){flags|=0x8000u;error=wsaEmsgsize;}
    if(op.address&&!storeAddress(op.address,op.addressSize,peer,std::min<socklen_t>(message.msg_namelen,16)))error=wsaEfault;
    return true;
}
void cancelReceives(uint32_t socket){
    for(auto it=receives.begin();it!=receives.end();)if(it->second.socket==socket){complete(it->second,995,0,0);it=receives.erase(it);}else ++it;
}
}
void pollGuestNetwork(){
    for(auto it=net::receives.begin();it!=net::receives.end();){
        uint32_t bytes=0,flags=0,error=0;
        if(net::receive(it->second,bytes,flags,error)){net::complete(it->second,error,bytes,flags);it=net::receives.erase(it);}else ++it;
    }
}
void wsaCreateEvent(){
    // OVERLAPPED reserves hEvent's low bit. Keep it clear on real handles so
    // successive events cannot alias when the completion path masks that bit.
    uint32_t handle=(nextEventHandle+3u)&~3u;nextEventHandle=handle+4u;
    eventHandles.emplace(handle,std::make_shared<GuestEvent>(GuestEvent{true,false}));ret(handle,0);
}
void wsaCloseEvent(){if(!eventHandles.erase(arg(0))){lastError=6;ret(0,1);return;}ret(1,1);}
void wsaResetEvent(){resetEvent();}
void wsaSetEvent(){setEvent();}
void wsaWaitForMultipleEvents(){
    // Same wait semantics and five-argument ABI as WaitForMultipleObjectsEx.
    // No APC completion routines are registered by the event-based receive path.
    waitMultiple();g_esp+=4;
}
void wsaReceive(bool from){
    const uint32_t argc=from?9u:7u;
    uint32_t handle=arg(0),array=arg(1),count=arg(2),bytesOut=arg(3),flagsOut=arg(4);
    uint32_t overlapped=arg(from?7:5),routine=arg(from?8:6);
    auto* socket=net::find(handle);
    if(!socket){ret(net::fail(net::wsaEnotsock),argc);return;}
    if(!array||!count||count>64||!flagsOut||(!overlapped&&!bytesOut)){ret(net::fail(net::wsaEfault),argc);return;}
    if(routine||socket->icmp){ret(net::fail(10045),argc);return;}
    net::Receive op{handle,overlapped,read32(flagsOut),flagsOut,from?arg(5):0,from?arg(6):0,0,{}};
    if(op.flags&~11u){ret(net::fail(10045),argc);return;}
    if(op.address&&(!op.addressSize||read32(op.addressSize)<16)){ret(net::fail(net::wsaEfault),argc);return;}
    uint64_t total=0;
    for(uint32_t i=0;i<count;++i){uint32_t length=read32(array+i*8),address=read32(array+i*8+4);
        if(length)ptr(address,length);total+=length;op.buffers.emplace_back(address,length);}
    if(total>0x7fffffffu){ret(net::fail(net::wsaEinval),argc);return;}
    if(overlapped){
        ptr(overlapped,20);if(net::receives.count(overlapped)){ret(net::fail(net::wsaEinval),argc);return;}
        op.event=read32(overlapped+16);
        if(op.event&&!eventHandles.count(op.event&~1u)){ret(net::fail(6),argc);return;}
        if(op.event)eventHandles.at(op.event&~1u)->signaled=false;
        net::completedFlags.erase(overlapped);
    }
    uint32_t bytes=0,flags=0,error=0;
    while(!net::receive(op,bytes,flags,error)){
        if(overlapped){
            write32(overlapped,997);write32(overlapped+4,0);net::receives.emplace(overlapped,std::move(op));
            ret(net::fail(997),argc);return;
        }
        socket=net::find(handle);
        if(!socket){ret(net::fail(net::wsaEnotsock),argc);return;}
        if(socket->guestNonBlocking){ret(net::fail(net::wsaEwouldblock),argc);return;}
        if(!net::wait(socket->fd,POLLIN,socket->recvTimeout)){ret(net::fail(net::wsaEtimedout),argc);return;}
    }
    if(bytesOut)write32(bytesOut,bytes);write32(flagsOut,flags);
    if(overlapped)net::complete(op,error,bytes,flags);
    ret(error?net::fail(error):0,argc);
}
void wsaRecv(){wsaReceive(false);}
void wsaRecvFrom(){wsaReceive(true);}
void wsaGetOverlappedResult(){
    uint32_t overlapped=arg(1),bytes=arg(2),wait=arg(3),flags=arg(4);
    if(!net::find(arg(0))){lastError=net::wsaEnotsock;ret(0,5);return;}
    if(!overlapped||!bytes||!flags){lastError=net::wsaEfault;ret(0,5);return;}
    while(read32(overlapped)==997){pollGuestNetwork();if(read32(overlapped)!=997)break;
        if(!wait){lastError=996;ret(0,5);return;}checkGuestProgress();yieldGuestMachine(1);}
    write32(bytes,read32(overlapped+4));write32(flags,net::completedFlags[overlapped]);
    uint32_t error=read32(overlapped);if(error)lastError=error;ret(error?0:1,5);
}

void testGuestNetwork(){
    State original;original.save();uint32_t savedError=lastError;
    uint32_t data=allocate(1024),event=0,secondEvent=0,handle=0;int sender=-1,receiver=-1;
    auto invoke=[&](recomp_func_t function,std::initializer_list<uint32_t> args){
        State saved;saved.save();uint32_t frame=data+512;write32(frame,0);
        uint32_t i=0;for(auto value:args)write32(frame+4+4*i++,value);
        g_esp=frame;function();uint32_t result=g_eax;
        if(g_esp!=frame+4+4*args.size())throw GuestStop("Winsock event ABI stack check failed");
        saved.load();return result;
    };
    auto require=[](bool ok){if(!ok)throw GuestStop("Winsock overlapped receive regression failed");};
    try{
        // Windows FD_ISSET searches opaque SOCKET handles, not Linux fd bits.
        write32(data,0);require(invoke(socketFdIsSet,{0x12345678,data})==0);
        write32(data,3);write32(data+4,0x12345678);write32(data+8,0x87654321);write32(data+12,0xf0000010);
        for(uint32_t value:{0x12345678u,0x87654321u,0xf0000010u})require(invoke(socketFdIsSet,{value,data})==1);
        require(invoke(socketFdIsSet,{42,data})==0&&invoke(socketFdIsSet,{42,0})==0);
        require(read32(data)==3&&read32(data+12)==0xf0000010);
        // Exercise the actual NetBIOS guest ABI and its adapter-status layout.
        // Repeated calls must return this installation's same unicast MAC,
        // without overwriting the status buffer's surrounding guard bytes.
        std::memset(ptr(data+256,64),0,64);
        *static_cast<uint8_t*>(ptr(data+256,1))=0x33;
        write32(data+260,data+320);uint16_t statusSize=64;
        std::memcpy(ptr(data+264,2),&statusSize,2);
        for(unsigned repeat=0;repeat<2;++repeat){
            std::memset(ptr(data+320,68),0xc3,68);
            require(invoke(netbios,{data+256})==0);
            require(!std::memcmp(ptr(data+320,6),guestNetworkMac.data(),6));
            require((guestNetworkMac[0]&3u)==2u&&read32(data+384)==0xc3c3c3c3u);
            require(*static_cast<uint8_t*>(ptr(data+257,1))==0&&*static_cast<uint8_t*>(ptr(data+305,1))==0);
            uint16_t written=0;std::memcpy(&written,ptr(data+264,2),2);require(written==60);
            for(unsigned i=6;i<64;++i)require(*static_cast<uint8_t*>(ptr(data+320+i,1))==0);
            std::memcpy(ptr(data+264,2),&statusSize,2);
        }
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","NetBIOS identity passed: stable local unicast address, status layout, buffer guards and guest ABI");
        receiver=::socket(AF_INET,SOCK_DGRAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
        sender=::socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);require(receiver>=0&&sender>=0);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        require(::bind(receiver,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);
        socklen_t size=sizeof address;require(::getsockname(receiver,reinterpret_cast<sockaddr*>(&address),&size)==0);
        handle=net::newHandle(receiver,true);receiver=-1;
        event=invoke(wsaCreateEvent,{});write32(data,event);
        secondEvent=invoke(wsaCreateEvent,{});write32(data+4,secondEvent);
        require(event!=secondEvent&&!(event&3u)&&!(secondEvent&3u));
        require(invoke(wsaSetEvent,{secondEvent})==1);
        require(invoke(wsaWaitForMultipleEvents,{2,data,0,0,0})==1);
        require(invoke(wsaCloseEvent,{secondEvent})==1);secondEvent=0;
        require(invoke(wsaWaitForMultipleEvents,{1,data,0,0,0})==258);
        require(invoke(wsaWaitForMultipleEvents,{0,data,0,0,0})==UINT32_MAX&&lastError==87);
        require(invoke(wsaSetEvent,{event})==1&&invoke(wsaWaitForMultipleEvents,{1,data,0,0,0})==0);
        require(invoke(wsaResetEvent,{event})==1);
        // Two WSABUF entries exercise scatter receive and 32-bit guest pointers.
        write32(data+16,2);write32(data+20,data+128);write32(data+24,2);write32(data+28,data+130);
        std::memset(ptr(data+64,20),0,20);write32(data+80,event);write32(data+96,0);write32(data+100,16);
        require(invoke(wsaRecvFrom,{handle,data+16,2,0,data+96,data+160,data+100,data+64,0})==UINT32_MAX&&lastError==997);
        require(invoke(wsaGetOverlappedResult,{handle,data+64,data+104,0,data+108})==0&&lastError==996);
        require(invoke(wsaWaitForMultipleEvents,{1,data,0,0,0})==258);
        require(::sendto(sender,"NFS",3,0,reinterpret_cast<sockaddr*>(&address),sizeof address)==3);
        require(invoke(wsaWaitForMultipleEvents,{1,data,0,100,0})==0);
        require(invoke(wsaGetOverlappedResult,{handle,data+64,data+104,0,data+108})==1);
        require(read32(data+104)==3&&read32(data+108)==0&&!std::memcmp(ptr(data+128,3),"NFS",3)&&read32(data+100)==16);
        require(invoke(wsaResetEvent,{event})==1);
        require(invoke(wsaRecv,{handle,data+16,2,0,data+96,data+64,0})==UINT32_MAX&&lastError==997);
        require(invoke(socketClose,{handle})==0);handle=0;
        require(read32(data+64)==995&&invoke(wsaWaitForMultipleEvents,{1,data,0,0,0})==0&&net::receives.empty());
        require(invoke(wsaCloseEvent,{event})==1);event=0;
        // Reproduce a lobby disconnect: WSACloseEvent runs on another thread
        // while WSAWaitForMultipleEvents is blocked with the machine released.
        // A ready auto-reset event must not be consumed when another entry is
        // invalid, regardless of its position or wait-any/wait-all mode.
        for(uint32_t all:{0u,1u})for(bool closeFirst:{false,true}){
            event=invoke(wsaCreateEvent,{});secondEvent=invoke(wsaCreateEvent,{});
            auto survivor=eventHandles.find(closeFirst?secondEvent:event)->second;
            survivor->manual=false;
            write32(data,event);write32(data+4,secondEvent);
            uint32_t closing=closeFirst?event:secondEvent,result=0,waitError=0;
            bool closed=false;std::exception_ptr failure;
            std::thread closer([&]{
                std::unique_lock<MachineMutex> lease(machine);
                try{closed=invoke(wsaCloseEvent,{closing})==1;survivor->signaled=true;}
                catch(...){failure=std::current_exception();}
            });
            try{result=invoke(wsaWaitForMultipleEvents,{2,data,all,1000,0});waitError=lastError;}
            catch(...){net::released([&]{closer.join();return 0;});throw;}
            net::released([&]{closer.join();return 0;});
            if(failure)std::rethrow_exception(failure);
            require(closed&&result==UINT32_MAX&&waitError==6&&survivor->signaled);
            if(closeFirst)event=0;else secondEvent=0;
            uint32_t remaining=closeFirst?secondEvent:event;write32(data,remaining);
            require(invoke(wsaWaitForMultipleEvents,{1,data,0,0,0})==0&&!survivor->signaled);
            require(invoke(wsaCloseEvent,{remaining})==1);event=secondEvent=0;
        }
        net::completedFlags.erase(data+64);
        ::close(sender);sender=-1;heaps->free(GuestHeaps::process,data);original.load();lastError=savedError;
        NFS_RUNTIME_LOG(ANDROID_LOG_INFO,"NFSC","Winsock events passed: timeout, signal/reset, pending UDP scatter receive, completion, cancellation, concurrent event close (any/all, first/last), signal preservation and ABI stack");
    }catch(...){
        if(handle){net::cancelReceives(handle);::close(net::sockets.at(handle).fd);net::sockets.erase(handle);}
        if(event)eventHandles.erase(event);if(secondEvent)eventHandles.erase(secondEvent);net::completedFlags.erase(data+64);
        if(receiver>=0)::close(receiver);if(sender>=0)::close(sender);
        heaps->free(GuestHeaps::process,data);original.load();lastError=savedError;throw;
    }
}
