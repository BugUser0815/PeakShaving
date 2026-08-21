#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <AddressConversion.hpp>
#include <LocalHost.hpp>
#include <ObisData.hpp>
#include <SpeedwireEmeterProtocol.hpp>
#include <SpeedwireHeader.hpp>
#include <SpeedwireSocketFactory.hpp>
#include <SpeedwireTagHeader.hpp>
using namespace libspeedwire;

namespace {
constexpr uint16_t SUSY_ID=349;
constexpr uint32_t SERIAL_NUMBER=1901567279;
constexpr size_t UDP_PACKET_SIZE=608;
constexpr uint16_t PROTOCOL_ID=SpeedwireData2Packet::sma_emeter_protocol_id;

struct ModbusTcp {
    std::string host; uint16_t port; uint8_t unit; uint16_t tx=1;
    std::vector<uint16_t> read(uint16_t start,uint16_t count){
        addrinfo hints{}; hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM; addrinfo* res=nullptr;
        auto ps=std::to_string(port); if(getaddrinfo(host.c_str(),ps.c_str(),&hints,&res)!=0||!res) throw std::runtime_error("getaddrinfo");
        int fd=socket(res->ai_family,res->ai_socktype,res->ai_protocol); if(fd<0){freeaddrinfo(res);throw std::runtime_error("socket");}
        timeval tv{}; tv.tv_sec=2; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)); setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
        if(connect(fd,res->ai_addr,res->ai_addrlen)!=0){auto e=std::string(strerror(errno));close(fd);freeaddrinfo(res);throw std::runtime_error("connect: "+e);} freeaddrinfo(res);
        std::array<uint8_t,12> q{}; q[0]=tx>>8;q[1]=tx;q[5]=6;q[6]=unit;q[7]=3;q[8]=start>>8;q[9]=start;q[10]=count>>8;q[11]=count; ++tx;
        if(send(fd,q.data(),q.size(),0)!=(ssize_t)q.size()){close(fd);throw std::runtime_error("send");}
        auto recvAll=[&](uint8_t* p,size_t n){size_t got=0;while(got<n){ssize_t r=recv(fd,p+got,n-got,0);if(r<=0)throw std::runtime_error("recv");got+=r;}};
        std::array<uint8_t,9> h{}; try{recvAll(h.data(),h.size());}catch(...){close(fd);throw;}
        if(h[7]&0x80){close(fd);throw std::runtime_error("modbus exception "+std::to_string(h[8]));}
        if(h[7]!=3||h[8]!=count*2){close(fd);throw std::runtime_error("unexpected response");}
        std::vector<uint8_t> d(count*2); try{recvAll(d.data(),d.size());}catch(...){close(fd);throw;} close(fd);
        std::vector<uint16_t> r(count); for(size_t i=0;i<r.size();++i) r[i]=(uint16_t(d[i*2])<<8)|d[i*2+1]; return r;
    }
};

uint32_t oldU32(const std::vector<uint16_t>& b,uint16_t base,uint16_t addr){size_t i=addr-base+1;if(i>=b.size())throw std::runtime_error("register range");return b[i];}
struct Values{std::array<double,35> v{};};

Values readKsem(ModbusTcp& mb,double peakW){
    auto total=mb.read(0,28), l1=mb.read(40,25), l2=mb.read(80,25), l3=mb.read(120,25); Values x;
    const uint16_t bases[3]={40,80,120}; const std::vector<uint16_t>* blocks[3]={&l1,&l2,&l3}; const uint16_t off[9]={0,2,4,6,16,18,20,22,24};
    for(int p=0;p<3;++p)for(int j=0;j<9;++j)x.v[p*9+j]=oldU32(*blocks[p],bases[p],bases[p]+off[j]);
    double real=x.v[0]+x.v[9]+x.v[18], peak=peakW*10.0;
    if(real/10.0<peakW){x.v[27]=0;x.v[28]=std::max(0.0,peak-real);}else{x.v[27]=real-peak;x.v[28]=0;}
    x.v[29]=oldU32(total,0,4);x.v[30]=oldU32(total,0,6);x.v[31]=oldU32(total,0,16);x.v[32]=oldU32(total,0,18);x.v[33]=oldU32(total,0,24);x.v[34]=oldU32(total,0,26);return x;
}

void* put(SpeedwireEmeterProtocol& p,void* o,const ObisData& s,double v){ObisData t(s);t.measurementValues.addMeasurement(v,0);auto a=t.toByteArray();return p.setObisElement(o,a.data());}
void* put(SpeedwireEmeterProtocol& p,void* o,const ObisData& s,const std::string& v){ObisData t(s);t.measurementValues.value_string=v;auto a=t.toByteArray();return p.setObisElement(o,a.data());}

void sendSma(const Values& x){
    LocalHost& lh=LocalHost::getInstance(); uint8_t udp[UDP_PACKET_SIZE]{}; SpeedwireHeader h(udp,sizeof(udp)); auto hl=h.getDefaultHeaderTotalLength(1,0,0); h.setDefaultHeader(1,uint16_t(UDP_PACKET_SIZE-hl),PROTOCOL_ID);
    auto* end=(uint8_t*)h.findTagPacket(SpeedwireTagHeader::sma_tag_endofdata); SpeedwireData2Packet d(h); SpeedwireEmeterProtocol m(d); m.setSusyID(SUSY_ID);m.setSerialNumber(SERIAL_NUMBER);m.setTime((uint32_t)lh.getUnixEpochTimeInMs()); void* o=const_cast<void*>(m.getFirstObisElement());
    o=put(m,o,ObisData::PositiveActivePowerTotal,x.v[27]/10);o=put(m,o,ObisData::PositiveActiveEnergyTotal,0.0);o=put(m,o,ObisData::NegativeActivePowerTotal,x.v[28]/10);o=put(m,o,ObisData::NegativeActiveEnergyTotal,0.0);
    o=put(m,o,ObisData::PositiveReactivePowerTotal,x.v[29]/10);o=put(m,o,ObisData::PositiveReactiveEnergyTotal,0.0);o=put(m,o,ObisData::NegativeReactivePowerTotal,x.v[30]/10);o=put(m,o,ObisData::NegativeReactiveEnergyTotal,0.0);
    o=put(m,o,ObisData::PositiveApparentPowerTotal,x.v[31]/10);o=put(m,o,ObisData::PositiveApparentEnergyTotal,0.0);o=put(m,o,ObisData::NegativeApparentPowerTotal,x.v[32]/10);o=put(m,o,ObisData::NegativeApparentEnergyTotal,0.0);o=put(m,o,ObisData::PowerFactorTotal,x.v[33]/1000);o=put(m,o,ObisData::Frequency,x.v[34]/1000);
    o=put(m,o,ObisData::PositiveActivePowerL1,x.v[0]);o=put(m,o,ObisData::PositiveActiveEnergyL1,0.0);o=put(m,o,ObisData::NegativeActivePowerL1,x.v[1]/10);o=put(m,o,ObisData::NegativeActiveEnergyL1,0.0);o=put(m,o,ObisData::PositiveReactivePowerL1,x.v[2]/10);o=put(m,o,ObisData::PositiveReactiveEnergyL1,0.0);o=put(m,o,ObisData::NegativeReactivePowerL1,x.v[3]/10);o=put(m,o,ObisData::NegativeReactiveEnergyL1,0.0);o=put(m,o,ObisData::PositiveApparentPowerL1,x.v[4]/10);o=put(m,o,ObisData::PositiveApparentEnergyL1,0.0);o=put(m,o,ObisData::NegativeApparentPowerL1,x.v[5]/10);o=put(m,o,ObisData::NegativeApparentEnergyL1,0.0);o=put(m,o,ObisData::CurrentL1,x.v[6]);o=put(m,o,ObisData::VoltageL1,(x.v[7]/1000+200)*1000);o=put(m,o,ObisData::PowerFactorL1,x.v[8]/1000);
    o=put(m,o,ObisData::PositiveActivePowerL2,x.v[9]/10);o=put(m,o,ObisData::PositiveActiveEnergyL2,0.0);o=put(m,o,ObisData::NegativeActivePowerL2,x.v[10]/10);o=put(m,o,ObisData::NegativeActiveEnergyL2,0.0);o=put(m,o,ObisData::PositiveReactivePowerL2,x.v[11]/10);o=put(m,o,ObisData::PositiveReactiveEnergyL2,0.0);o=put(m,o,ObisData::NegativeReactivePowerL2,x.v[12]/10);o=put(m,o,ObisData::NegativeReactiveEnergyL2,0.0);o=put(m,o,ObisData::PositiveApparentPowerL2,x.v[13]/10);o=put(m,o,ObisData::PositiveApparentEnergyL2,0.0);o=put(m,o,ObisData::NegativeApparentPowerL2,x.v[14]/10);o=put(m,o,ObisData::NegativeApparentEnergyL2,0.0);o=put(m,o,ObisData::CurrentL2,x.v[15]);o=put(m,o,ObisData::VoltageL2,(x.v[16]/1000+200)*1000);o=put(m,o,ObisData::PowerFactorL2,x.v[17]/1000);
    o=put(m,o,ObisData::PositiveActivePowerL3,x.v[18]/10);o=put(m,o,ObisData::PositiveActiveEnergyL3,0.0);o=put(m,o,ObisData::NegativeActivePowerL3,x.v[19]/10);o=put(m,o,ObisData::NegativeActiveEnergyL3,0.0);o=put(m,o,ObisData::PositiveReactivePowerL3,x.v[20]/10);o=put(m,o,ObisData::PositiveReactiveEnergyL3,0.0);o=put(m,o,ObisData::NegativeReactivePowerL3,x.v[21]/10);o=put(m,o,ObisData::NegativeReactiveEnergyL3,0.0);o=put(m,o,ObisData::PositiveApparentPowerL3,x.v[22]/10);o=put(m,o,ObisData::PositiveApparentEnergyL3,0.0);o=put(m,o,ObisData::NegativeApparentPowerL3,x.v[23]/10);o=put(m,o,ObisData::NegativeApparentEnergyL3,0.0);o=put(m,o,ObisData::CurrentL3,x.v[24]);o=put(m,o,ObisData::VoltageL3,(x.v[25]/1000+200)*1000);o=put(m,o,ObisData::PowerFactorL3,x.v[26]/1000);o=put(m,o,ObisData::SoftwareVersion,std::string("2.03.4.R"));
    if(o!=end)throw std::runtime_error("SMA packet size mismatch"); m.setTime((uint32_t)lh.getUnixEpochTimeInMs());
    for(const auto& ip:lh.getLocalIPv4Addresses()){SpeedwireSocket s=SpeedwireSocketFactory::getInstance(lh)->getSendSocket(SpeedwireSocketFactory::SocketType::MULTICAST,ip);int n=s.sendto(udp,sizeof(udp),s.getSpeedwireMulticastIn4Address(),AddressConversion::toInAddress(ip));if(n!=(int)sizeof(udp))throw std::runtime_error("multicast send");}
}
}

int main(int argc,char** argv){
    std::string host=argc>1?argv[1]:"10.0.0.70"; double peak=argc>2?std::stod(argv[2]):11000.0; uint16_t port=argc>3?std::stoi(argv[3]):502; uint8_t unit=argc>4?std::stoi(argv[4]):71; ModbusTcp mb{host,port,unit};
    std::cerr<<"KSEM "<<host<<":"<<port<<" unit="<<unsigned(unit)<<" peak="<<peak<<"W\n";
    for(;;){try{auto x=readKsem(mb,peak);sendSma(x);std::cerr<<"real="<<(x.v[0]+x.v[9]+x.v[18])/10<<"W fake_import="<<x.v[27]/10<<"W fake_export="<<x.v[28]/10<<"W\n";}catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<"\n";}LocalHost::sleep(1000);}
}
