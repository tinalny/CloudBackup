#include "cloud_backup.hpp"
#include <thread>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

void compress_test(char *argv[])
{
    //argv[1] = 源文件名称
    //argv[2] = 压缩包名称
    cloud_backup::CompressUtil::Compress(argv[1],argv[2]);
    std::string file = argv[2];
    file += ".txt";
    cloud_backup::CompressUtil::Compress(argv[2],file.c_str());
}

void data_test()
{
    cloud_backup::DataManagement data_management("./test.txt");
    data_management.InitLoad();
    data_management.Insert("c.txt","c.txt.gz");
    std::vector<std::string> list;

    data_management.GetAllName(&list);
    for(auto i:list)
    {
        printf("%s\n",i.c_str());
    }
    printf("--------------------\n");
    list.clear();
    data_management.NonCompressList(&list);
    for(auto i:list)
    {
        printf("%s\n",i.c_str());
    }
}

void m_non_compress()
{
    cloud_backup::NonHotCompress ncom(GZFILE_DIR);
    ncom.Start();
    return;
}

void thr_http_server()
{
    cloud_backup::Server srv;
    srv.Start();
    return;
}
int main()
{
    //文件备份路径不存在则创建
    if(boost::filesystem::exists(GZFILE_DIR) == false)
    {
        boost::filesystem::create_directory(GZFILE_DIR);
    }

    //压缩包存放路径不存在则创建
    if(boost::filesystem::exists(BACKUP_DIR) == false)
    {
        boost::filesystem::create_directory(BACKUP_DIR);
    }
    std::thread thr_compress(m_non_compress);//C++ 11中的线程,启动非热点文件压缩
    std::thread thr_server(thr_http_server);//网络通信服务端模块起启动
    thr_compress.join();//等待线程退出
    thr_server.join();
    return 0;
}
