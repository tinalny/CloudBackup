#include <stdio.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <zlib.h>
#include <pthread.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "httplib.h"

namespace cloud_backup{
#define NONHOT_TOME 10  //最后一次访问时间在10秒以外
#define INTERVAL_TIME 30   //非热点的检测每30秒进行一次
#define BACKUP_DIR "./backup/" //备份文件的存放路径
#define GZFILE_DIR "./gzfile/" //压缩包的存放路径
#define DATA_FILE "./list.backup" //数据管理模块的数据备份文件的名称
DataManagement data_management(DATA_FILE);

    class FileUtil//文件工具类
    {
        public:
            //从文件中读取所有内容
            static bool Read(const std::string &name,std::string *body)
            {
               //以二进制方式打开文件
                std::ifstream fs(name,std::ios::binary);
                if(fs.is_open() == false)
                {
                    std::cout<<"open file"<<name<<"failed"<<std::endl;
                    return false;
                }
                //boost::filesystem::file::size() 获取文件大小
                int64_t fsize = boost::filesystem::file_size(name);
                body->resize(fsize);
                fs.read(&(*body)[0],fsize);
                //判断上一次操作是否成功
                if(fs.good() == false)
                {
                    std::cout<<"file"<<name<<"read data fialed"<<std::endl;
                    return false;
                }
                fs.close();
                return true;
            }
            //向文件中写入数据
            static bool Write(const std::string &name,const std::string &body)
            {
                //ofstream默认打开文件就清空原有内容(覆盖式写入)
                std::ofstream ofs(name,std::ios::binary);
                if(ofs.is_open() == false)
                {
                    std::cout<<"open file"<<name<<"failed"<<std::endl;
                    return false;
                }

                ofs.write(&body[0],body.size());
                if(ofs.good() == false)
                {
                    std::cout<<"file"<<name<<"write data failed"<<std::endl;
                    return false;
                }
                ofs.close();
                return true;
            }
    };
    
    //文件压缩模块
    class CompressUtil
    {
        public:
            //文件压缩（源文件名称，压缩包名称）
            static bool Compress(const std::string &src,const std::string &dst)
            {
                std::string body;
                FileUtil::Read(src, &body);

                //打开压缩包
                gzFile gf = gzopen(dst.c_str(),"wb");
                if(gf == NULL)
                {
                    std::cout<<"file open"<<dst<<"failed"<<std::endl;
                    return false;
                }

                int wlen = 0;
                while(wlen < body.size())
                {
                    //gzwrite(文件描述符，写入数据，写入数据的大小)
                    int ret = gzwrite(gf,&body[wlen],body.size()-wlen);
                    if(ret == 0)
                    {
                        std::cout<<"file"<<dst<<"write compress data failed"<<std::endl;
                        return false;
                    }
                    wlen += ret;
                }
                gzclose(gf);
                return true;
            }
            //文件解压缩（压缩包名称，源文件名称）
            static bool UnCompress(const std::string &src, const std::string &dst)
            {
                std::ofstream ofs(dst,std::ios::binary);
                if(ofs.is_open() == false)
                {
                    std::cout<<"file open"<<dst<<"failed"<<std::endl;
                    return false;
                }

                gzFile gf = gzopen(src.c_str(),"rb");
                if(gf == NULL)
                {
                    std::cout<<"file open"<< src<<"failed"<<std::endl;
                    ofs.close();
                    return false;
                }

                int ret = 0;
                char tmp[4096]={0};
                //gzread(文件描述符，缓冲区，缓冲区大小)，成功返回实际读取的字节数，失败返回-1
                while((ret = gzread(gf,tmp,4096))>0 )
                {
                    ofs.write(tmp,ret);
                }

                ofs.close();
                gzclose(gf);
                return true;
            }
    };

    //数据管理模块
    class DataManagement
    {
        public:
            DataManagement(const std::string &path):
                _back_file(path)
            {
                pthread_rwlock_init(&_rwlock,NULL);
            }
            
            ~DataManagement()
            {
                pthread_rwlock_destroy(&_rwlock);
            }

            //判断文件是否存在
            bool Exist(const std::string &name)
            {
                //判断是否能够从_file_list中找到这个文件信息
                pthread_rwlock_rdlock(&_rwlock);
                auto it = _file_list.find(name);
                //查找到文列表末尾都没有找到该文件，说明不存在该文
                if(it == _file_list.end())
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;
                }
                pthread_rwlock_unlock(&_rwlock);
                return false;
            }

            //判断文件是否被压缩
            bool IsCompress(const std::string &name)
            {
                //在文件没压缩时，源文件名称与压缩包名称一致，都是源文件名称
                //文件压缩后，压缩包名称更新
                //若两个文件名称一致则表示文件未压缩，若不一致则表示文件被压缩
                //用一个unordered_map来存储文件列表
                
                //因为_file_list在不同的模块中使用，为了保证线程安全需要加读写锁，此处加的是读锁，因为只进行判断文件是否被压缩，只进行了访问，并未更改
                
                pthread_rwlock_rdlock(&_rwlock);
                //先判断文件是否存在
                auto it = _file_list.find(name);
                if(it == _file_list.end())
                {
                    pthread_rwlock_unlock(&_rwlock);
                    return false;
                }

                //若两个文件名称一致，则表示未被压缩
                if(it->first == it->second)
                {
                    pthread_rwlock_rdlock(&_rwlock);
                    return false;
                }
                pthread_rwlock_rdlock(&_rwlock);
                return true;
            }

            //获取未压缩文件列表
            bool NonCompressList(std::vector<std::string> *list)
            {
                //遍历_file_list，将没有被压缩的文件添加到list中
                pthread_rwlock_rdlock(&_rwlock);
                auto it = _file_list.begin();
                for(;it != _file_list.end(); it++)
                {
                    if(it->first == it->second)
                    {
                        list->push_back(it->first); 
                    }
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;
            }

            //插入/更新数据
            bool Insert(const std::string &src, const std::string &dst)
            {
                pthread_rwlock_wrlock(&_rwlock);
                _file_list[src] = dst;
                pthread_rwlock_unlock(&_rwlock);
                Storage();
                return true;
            }

            //获取所有文件名称
            bool GetAllName(std::vector<std::string>*list)
            {
                //只访问不修改，则添加的是读锁
                pthread_rwlock_rdlock(&_rwlock);
                auto it = _file_list.begin();
                for(;it != _file_list.end(); it++)
                {
                    //客户端只能看到源文件名称，所以向外展示的只是源文件名称，不是压缩包名称
                    list->push_back(it->first);
                }
                pthread_rwlock_unlock(&_rwlock);
                return true;
            }

            //数据改变后持久化存储
            bool Storage()
            {
                //将_file_list中的数据对象进行数据化存储----序列化
                //序列化存储格式：src dst\r\n
                pthread_rwlock_rdlock(&_rwlock);
                std::stringstream tmp;//实例化一个string流对象
                auto it = _file_list.begin();
                for(;it != _file_list.end(); it++)
                {
                    tmp << it->first <<" "<< it->second <<"\r\n";
                }
                pthread_rwlock_unlock(&_rwlock);
                //获取的文件列表写入文件，用于持久化存储
                FileUtil::Write(_back_file,tmp.str());
                return true;
            }

            //启动时初始化加载原有数据
            //filename gzfilename\r\nfilename gzfilename\r\n....
            bool InitLoad()
            {
                //从持久化存储文件中加载数据
                //1.将这个备份文件的数据读取出来
                std::string body;
                if(FileUtil::Read(_back_file,&body) == false)
                {
                    return false;
                }
                //2.进行字符串处理，按照\r\n进行分割
                //boost::split(vector,src,sep,flag)
                std::vector<std::string> list;
                boost::split(list,body,boost::is_any_of("\r\n"),boost::token_compress_off);
                //3.每一行按照空格进行分割，前边是key，后边是val
                for(auto i : list)
                {
                    size_t pos = i.find(" ");
                    if(pos == std::string::npos)
                    {
                        continue;
                    }
                    std::string key = i.substr(0,pos);
                    std::string val = i.substr(pos+1);
                    //4.将key/val添加到_file_list中
                    Insert(key,val);
                }
                return true;
            }
        private:
            //持久化数据存储文件名称
            std::string _back_file;
            std::unordered_map<std::string,std::string> _file_list;//用来存储源文件名称与压缩包名称的文件列表
            pthread_rwlock_t _rwlock;//读写锁，应用于读共享写互斥的场景
    };

    //网络通信模块
    class NonHotCompress
    {
        public:
            NonHotCompress(const std::string dir_name):
                _gz_dir(dir_name)
            {
                
            }

            //总体向外界提供的功能接口，开始压缩模块
            bool Start()
            {
                //是一个循环的，持续的过程-每隔一段时间，判断有没有非热点文件，然后进行压缩
                //问题：什么文件是非热点文件--当前时间-最后一次访问时间>n秒
                while(1)
                {
                    //1.获取以下所有的未压缩的文件列表
                    std::vector<std::string> list;
                    data_management.NonCompressList(&list);
                    //2.逐个判断这个文件是否是热点文件
                    for(int i = 0; i < list.size(); i++)
                    {
                        bool ret = FileIsHot(list[i]);
                        if(ret == false)
                        {
                            //非热点文件则组织源文件的路径名称以及压缩包的路径名称，进行压缩存储
                            std::string src_name = BACKUP_DIR + list[i];//纯源文件名称
                            std::string dst_name = GZFILE_DIR + list[i] + ".gz";//纯压缩包名称
                             //3.如果是非热点文件，则压缩这个文件，删除源文件
                            if(CompressUtil::Compress(src_name,dst_name) == true)
                            {
                                data_management.Insert();//更新文件信息
                                unlink(src_name.c_str());//删除源文件
                            }
                        }
                    }
                    //4.休眠一会儿
                    sleep(INTERVAL_TIME);
                }
            }
        private:
            //判断是否为热点文件
            bool FileIsHot(const std::string &name);
        private:
            //压缩后的文件存储路径
            std::string _gz_dir;
    };

    class Server
    {
        public:
            Server()
            {

            }
            ~Server()
            {

            }
            //启动网络通次模块接口
            bool Start();
            private:
            //文件上传处理回调函数
            static void Upload(const httplib::Request &req, httplib::Response &rsp);
            //文件列表处理回调函数
            static void List(const httplib::Request &req, httplib::Response &rsp);
            //文件下载处理回调函数
            static void Download(const httplib::Request &req, httplib::Response &rsp);
            private:
            //文件上传备份路径
            std::string _filr_dir;
            httplib::Server _server;
    };
};
