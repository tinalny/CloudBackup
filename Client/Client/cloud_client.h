#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "httplib.h"
using namespace std;

class FileUtil//文件工具类
{
public:
	//从文件中读取所有内容
	static bool Read(const std::string &name, std::string *body)
	{
		//以二进制方式打开文件
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open file" << name << "failed" << std::endl;
			return false;
		}
		//boost::filesystem::file::size() 获取文件大小
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);
		fs.read(&(*body)[0], fsize);
		//判断上一次操作是否成功
		if (fs.good() == false)
		{
			std::cout << "file" << name << "read data fialed" << std::endl;
			return false;
		}
		fs.close();
		return true;
	}
	//向文件中写入数据
	static bool Write(const std::string &name, const std::string &body)
	{
		//ofstream默认打开文件就清空原有内容(覆盖式写入)
		std::ofstream ofs(name, std::ios::binary);
		if (ofs.is_open() == false)
		{
			std::cout << "open file" << name << "failed" << std::endl;
			return false;
		}

		ofs.write(&body[0], body.size());
		if (ofs.good() == false)
		{
			std::cout << "file" << name << "write data failed" << std::endl;
			return false;
		}
		ofs.close();
		return true;
	}
};

class DataManage
{
private:
	string _store_file;//持久化存储文件
	unordered_map<string, string> _backup_list;//备份的文件信息
public:
	DataManage(const string &filename) :
		_store_file(filename)
	{}

	bool Insert(const string &key, const string &val)//插入或更新Etag信息
	{
		_backup_list[key] = val;
		Storage();
		return true;
	} 

	bool GetEtag(const string &key, string *val)//通过文件名获取原有的Etag信息
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
		{
			return false;
		}
		*val = it->second;
		return true;
	}

	bool Storage()//持久化存储
	{
		//将_file_list中的数据对象进行数据化存储----序列化
		//序列化存储格式：filename etag\r\n
		std::stringstream tmp;//实例化一个string流对象
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); it++)
		{
			tmp << it->first << " " << it->second << "\r\n";
		}
		//获取的文件列表写入文件，用于持久化存储
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}

	bool InitLoad()//初始化加载原有数据
	{
		//从持久化存储文件中加载数据
		//1.将这个备份文件的数据读取出来
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.进行字符串处理，按照\r\n进行分割
		//boost::split(vector,src,sep,flag)
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.每一行按照空格进行分割，前边是key，后边是val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.将key/val添加到_file_list中
			Insert(key, val);
		}
		return true;
	}
};



class CloudClient
{
private:
	string _listen_dir;//监控的目录名称
	DataManage data_manage;
	char *_srv_ip;
	uint16_t _srv_port;
public:
	CloudClient(const string &filename, const string &store_file,
		char *srv_ip,uint16_t srv_port) :
		_srv_ip(srv_ip),
		_srv_port(srv_port),
		_listen_dir(filename),
		data_manage(store_file)
	{}

	//完成整体的文件备份流程
	bool Start()
	{
		data_manage.InitLoad();
		while (1)
		{
			vector<string> list;
			GetBackUpFileList(&list);//获取到所有需要备份的文件名称
			for (int i = 0; i < list.size(); i++)
			{
				string name = list[i];//纯文件名
				string pathname = _listen_dir + name;//文件的路径名
				cout << pathname << "is need to backup" << endl;

				 //读取文件数据，作为请求正文
				string body;
				FileUtil::Read(pathname, &body);
				//实例化Client对象准备发起HTTP上传文件请求
				httplib::Client client(_srv_ip, _srv_port);
				string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//这个文件上传备份失败了
					cout << pathname << "backup failed" << endl;
					continue;
				}
				string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);//备份成功则插入/更新信息
				cout << pathname << "backup success" << endl;
			}
			Sleep(1000);
		}
		return true;
	}

	bool GetBackUpFileList(vector<string> *list)//获取需要备份的文件列表
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directory(_listen_dir);//目录不存在则创建
		}
		//1.进行目录监控，获取指定目录下所有文件名称
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;

		for (; begin != end; begin++)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				//目录不需要进行备份
				//当前我们并不做多层级目录备份，遇到目录直接越过
				continue;
			}
			string pathname = begin->path().string();
			string name = begin->path().filename().string();
			string cur_etag, old_etag;

			//2.逐个文件计算自身当前的etag
			GetEtag(pathname, &cur_etag);
			//3.获取已经备份过得etag信息
			data_manage.GetEtag(name, &old_etag);
			//4.与data_manage中保存原有的etag进行比对
			//   1.没有找到原有的etag,新文件需要备份
			//   2.找到原有的etag，但是当前etag和原有etag不相等，需要备份
			//   3.找到原有etag，并且与当前的etag相等，则不需要备份
			if (cur_etag != old_etag)
			{
				list->push_back(name);//当前etag与原有etag信息不一致，需要备份
			}
		}
		return true;
	}

	//etag：文件大小，文件最后一次修改时间
	bool GetEtag(const string &pathname, string *etag)//计算文件的etag信息
	{
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = to_string(fsize) + "-" + to_string(mtime);

		return true;
	}
};