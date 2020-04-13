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

class FileUtil//�ļ�������
{
public:
	//���ļ��ж�ȡ��������
	static bool Read(const std::string &name, std::string *body)
	{
		//�Զ����Ʒ�ʽ���ļ�
		std::ifstream fs(name, std::ios::binary);
		if (fs.is_open() == false)
		{
			std::cout << "open file" << name << "failed" << std::endl;
			return false;
		}
		//boost::filesystem::file::size() ��ȡ�ļ���С
		int64_t fsize = boost::filesystem::file_size(name);
		body->resize(fsize);
		fs.read(&(*body)[0], fsize);
		//�ж���һ�β����Ƿ�ɹ�
		if (fs.good() == false)
		{
			std::cout << "file" << name << "read data fialed" << std::endl;
			return false;
		}
		fs.close();
		return true;
	}
	//���ļ���д������
	static bool Write(const std::string &name, const std::string &body)
	{
		//ofstreamĬ�ϴ��ļ������ԭ������(����ʽд��)
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
	string _store_file;//�־û��洢�ļ�
	unordered_map<string, string> _backup_list;//���ݵ��ļ���Ϣ
public:
	DataManage(const string &filename) :
		_store_file(filename)
	{}

	bool Insert(const string &key, const string &val)//��������Etag��Ϣ
	{
		_backup_list[key] = val;
		Storage();
		return true;
	} 

	bool GetEtag(const string &key, string *val)//ͨ���ļ�����ȡԭ�е�Etag��Ϣ
	{
		auto it = _backup_list.find(key);
		if (it == _backup_list.end())
		{
			return false;
		}
		*val = it->second;
		return true;
	}

	bool Storage()//�־û��洢
	{
		//��_file_list�е����ݶ���������ݻ��洢----���л�
		//���л��洢��ʽ��filename etag\r\n
		std::stringstream tmp;//ʵ����һ��string������
		auto it = _backup_list.begin();
		for (; it != _backup_list.end(); it++)
		{
			tmp << it->first << " " << it->second << "\r\n";
		}
		//��ȡ���ļ��б�д���ļ������ڳ־û��洢
		FileUtil::Write(_store_file, tmp.str());
		return true;
	}

	bool InitLoad()//��ʼ������ԭ������
	{
		//�ӳ־û��洢�ļ��м�������
		//1.����������ļ������ݶ�ȡ����
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false)
		{
			return false;
		}
		//2.�����ַ�����������\r\n���зָ�
		//boost::split(vector,src,sep,flag)
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//3.ÿһ�а��տո���зָǰ����key�������val
		for (auto i : list)
		{
			size_t pos = i.find(" ");
			if (pos == std::string::npos)
			{
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			//4.��key/val��ӵ�_file_list��
			Insert(key, val);
		}
		return true;
	}
};



class CloudClient
{
private:
	string _listen_dir;//��ص�Ŀ¼����
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

	//���������ļ���������
	bool Start()
	{
		data_manage.InitLoad();
		while (1)
		{
			vector<string> list;
			GetBackUpFileList(&list);//��ȡ��������Ҫ���ݵ��ļ�����
			for (int i = 0; i < list.size(); i++)
			{
				string name = list[i];//���ļ���
				string pathname = _listen_dir + name;//�ļ���·����
				cout << pathname << "is need to backup" << endl;

				 //��ȡ�ļ����ݣ���Ϊ��������
				string body;
				FileUtil::Read(pathname, &body);
				//ʵ����Client����׼������HTTP�ϴ��ļ�����
				httplib::Client client(_srv_ip, _srv_port);
				string req_path = "/" + name;
				auto rsp = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (rsp == NULL || (rsp != NULL && rsp->status != 200))
				{
					//����ļ��ϴ�����ʧ����
					cout << pathname << "backup failed" << endl;
					continue;
				}
				string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(name, etag);//���ݳɹ������/������Ϣ
				cout << pathname << "backup success" << endl;
			}
			Sleep(1000);
		}
		return true;
	}

	bool GetBackUpFileList(vector<string> *list)//��ȡ��Ҫ���ݵ��ļ��б�
	{
		if (boost::filesystem::exists(_listen_dir) == false)
		{
			boost::filesystem::create_directory(_listen_dir);//Ŀ¼�������򴴽�
		}
		//1.����Ŀ¼��أ���ȡָ��Ŀ¼�������ļ�����
		boost::filesystem::directory_iterator begin(_listen_dir);
		boost::filesystem::directory_iterator end;

		for (; begin != end; begin++)
		{
			if (boost::filesystem::is_directory(begin->status()))
			{
				//Ŀ¼����Ҫ���б���
				//��ǰ���ǲ�������㼶Ŀ¼���ݣ�����Ŀ¼ֱ��Խ��
				continue;
			}
			string pathname = begin->path().string();
			string name = begin->path().filename().string();
			string cur_etag, old_etag;

			//2.����ļ���������ǰ��etag
			GetEtag(pathname, &cur_etag);
			//3.��ȡ�Ѿ����ݹ���etag��Ϣ
			data_manage.GetEtag(name, &old_etag);
			//4.��data_manage�б���ԭ�е�etag���бȶ�
			//   1.û���ҵ�ԭ�е�etag,���ļ���Ҫ����
			//   2.�ҵ�ԭ�е�etag�����ǵ�ǰetag��ԭ��etag����ȣ���Ҫ����
			//   3.�ҵ�ԭ��etag�������뵱ǰ��etag��ȣ�����Ҫ����
			if (cur_etag != old_etag)
			{
				list->push_back(name);//��ǰetag��ԭ��etag��Ϣ��һ�£���Ҫ����
			}
		}
		return true;
	}

	//etag���ļ���С���ļ����һ���޸�ʱ��
	bool GetEtag(const string &pathname, string *etag)//�����ļ���etag��Ϣ
	{
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = to_string(fsize) + "-" + to_string(mtime);

		return true;
	}
};