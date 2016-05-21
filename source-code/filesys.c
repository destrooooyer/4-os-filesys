#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<string.h>
#include<ctype.h>
#include "filesys.h"


#define RevByte(low,high) ((high)<<8|(low))
#define RevWord(lowest,lower,higher,highest) ((highest)<< 24|(higher)<<16|(lower)<<8|lowest) 

/*
*功能：打印启动项记录
*/
void ScanBootSector()
{
	unsigned char buf[SECTOR_SIZE];
	int ret, i;

	if ((ret = read(fd, buf, SECTOR_SIZE)) < 0)		//fd是main开始打开的文件 "/dev/sdb1" 
		perror("read boot sector failed");
	for (i = 0; i < 8; i++)
		bdptor.Oem_name[i] = buf[i + 0x03];			//struct BootDescriptor_t bdptor
	bdptor.Oem_name[i] = '\0';

	bdptor.BytesPerSector = RevByte(buf[0x0b], buf[0x0c]);	//RevByte: ((high)<<8|(low))
	bdptor.SectorsPerCluster = buf[0x0d];
	bdptor.ReservedSectors = RevByte(buf[0x0e], buf[0x0f]);
	bdptor.FATs = buf[0x10];
	bdptor.RootDirEntries = RevByte(buf[0x11], buf[0x12]);
	bdptor.LogicSectors = RevByte(buf[0x13], buf[0x14]);
	bdptor.MediaType = buf[0x15];
	bdptor.SectorsPerFAT = RevByte(buf[0x16], buf[0x17]);
	bdptor.SectorsPerTrack = RevByte(buf[0x18], buf[0x19]);
	bdptor.Heads = RevByte(buf[0x1a], buf[0x1b]);
	bdptor.HiddenSectors = RevByte(buf[0x1c], buf[0x1d]);


	printf("Oem_name \t\t%s\n"
		"BytesPerSector \t\t%d\n"
		"SectorsPerCluster \t%d\n"
		"ReservedSector \t\t%d\n"
		"FATs \t\t\t%d\n"
		"RootDirEntries \t\t%d\n"
		"LogicSectors \t\t%d\n"
		"MedioType \t\t%d\n"
		"SectorPerFAT \t\t%d\n"
		"SectorPerTrack \t\t%d\n"
		"Heads \t\t\t%d\n"
		"HiddenSectors \t\t%d\n",
		bdptor.Oem_name,
		bdptor.BytesPerSector,
		bdptor.SectorsPerCluster,
		bdptor.ReservedSectors,
		bdptor.FATs,
		bdptor.RootDirEntries,
		bdptor.LogicSectors,
		bdptor.MediaType,
		bdptor.SectorsPerFAT,
		bdptor.SectorsPerTrack,
		bdptor.Heads,
		bdptor.HiddenSectors);
}

/*日期*/
void findDate(unsigned short *year,
	unsigned short *month,
	unsigned short *day,
	unsigned char info[2])
{
	int date;
	date = RevByte(info[0], info[1]);

	*year = ((date & MASK_YEAR) >> 9) + 1980;
	*month = ((date & MASK_MONTH) >> 5);
	*day = (date & MASK_DAY);
}

/*时间*/
void findTime(unsigned short *hour,
	unsigned short *min,
	unsigned short *sec,
	unsigned char info[2])
{
	int time;
	time = RevByte(info[0], info[1]);

	*hour = ((time & MASK_HOUR) >> 11);
	*min = (time & MASK_MIN) >> 5;
	*sec = (time & MASK_SEC) * 2;
}

/*
*文件名格式化，便于比较
*/
void FileNameFormat(unsigned char *name)
{
	unsigned char *p = name;
	while (*p != '\0')
		p++;
	p--;
	while (*p == ' ')
		p--;
	p++;
	*p = '\0';
}

/*参数：entry，类型：struct Entry*
*返回值：成功，则返回偏移值；失败：返回负值
*功能：从根目录或文件簇中得到文件表项
*/
int GetEntry(struct Entry *pentry)
{
	int ret, i;
	int count = 0;
	unsigned char buf[DIR_ENTRY_SIZE], info[2];

	/*读一个目录表项，即32字节*/
	if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
		perror("read entry failed");
	count += ret;

	//printf("//%d//\n", buf[0]);
	if (buf[0] == 0xe5 || buf[0] == 0x00)
		return -1 * count;
	else
	{
		/*长文件名，忽略掉*/
		while (buf[11] == 0x0f)
		{
			if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
				perror("read root dir failed");
			count += ret;
		}

		/*命名格式化，主义结尾的'\0'*/
		for (i = 0; i <= 10; i++)
			pentry->short_name[i] = buf[i];
		pentry->short_name[i] = '\0';

		FileNameFormat(pentry->short_name);



		info[0] = buf[22];
		info[1] = buf[23];
		findTime(&(pentry->hour), &(pentry->min), &(pentry->sec), info);

		info[0] = buf[24];
		info[1] = buf[25];
		findDate(&(pentry->year), &(pentry->month), &(pentry->day), info);

		pentry->FirstCluster = RevByte(buf[26], buf[27]);
		pentry->size = RevWord(buf[28], buf[29], buf[30], buf[31]);

		pentry->readonly = (buf[11] & ATTR_READONLY) ? 1 : 0;
		pentry->hidden = (buf[11] & ATTR_HIDDEN) ? 1 : 0;
		pentry->system = (buf[11] & ATTR_SYSTEM) ? 1 : 0;
		pentry->vlabel = (buf[11] & ATTR_VLABEL) ? 1 : 0;
		pentry->subdir = (buf[11] & ATTR_SUBDIR) ? 1 : 0;
		pentry->archive = (buf[11] & ATTR_ARCHIVE) ? 1 : 0;

		//printf("%s\n", pentry->short_name);

		return count;
	}
}

int scan()
{
	int ret, offset, cluster_addr;
	struct Entry entry;
	unsigned char buf[DIR_ENTRY_SIZE];
	if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
		perror("read entry failed");

	if (curdir == NULL)
	{
		/*将fd定位到根目录区的起始地址*/
		if ((ret = lseek(fd, ROOTDIR_OFFSET, SEEK_SET)) < 0)
			perror("lseek ROOTDIR_OFFSET failed");

		offset = ROOTDIR_OFFSET;

		/*从根目录区开始遍历，直到数据区起始地址*/
		while (offset < (DATA_OFFSET))
		{
			lseek(fd, offset, SEEK_SET);
			ret = GetEntry(&entry);

			offset += abs(ret);
			if (ret > 0 && entry.subdir)
			{
				short cur_cluster = entry.FirstCluster;
				while (1)
				{
					if (GetFatCluster(cur_cluster) == 0)
					{
						fatbuf[cur_cluster * 2] = 0xff;
						fatbuf[cur_cluster * 2 + 1] = 0xff;
						WriteFat();
					}
					if (GetFatCluster(cur_cluster) != 0xffff)
					{
						cur_cluster = GetFatCluster(cur_cluster);
					}
					else
					{
						break;
					}
				}
// 				if (curdir == NULL)
// 					curdir = &entry;
// 				printf("123\n");
// 				scan();
// 				printf("123\n");
// 				curdir = NULL;
// 				printf("123\n");
				struct Entry *cur_back_up = (struct Entry*)malloc(sizeof(struct Entry));
				if (curdir == NULL)
				{
					curdir = (struct Entry*)malloc(sizeof(struct Entry));
					memcpy(curdir, &entry, sizeof(struct Entry));
					scan();
					curdir = NULL;
				}
				else
				{
					memcpy(cur_back_up, curdir, sizeof(struct Entry));
					memcpy(curdir, &entry, sizeof(struct Entry));
					scan();
					memcpy(curdir, cur_back_up, sizeof(struct Entry));
				}
			}
		}
	}

	else /*显示子目录*/
	{
		//读取目录的所有cluster,而不是只读第一个cluster
		short cur_cluster = curdir->FirstCluster;
		//printf("%d\n", curdir->FirstCluster);
		while (1)
		{

			cluster_addr = DATA_OFFSET + (cur_cluster - 2) * CLUSTER_SIZE;
			if ((ret = lseek(fd, cluster_addr, SEEK_SET)) < 0)
				perror("lseek cluster_addr failed");

			offset = cluster_addr;
			if (GetFatCluster(cur_cluster) == 0)
			{
				fatbuf[cur_cluster * 2] = 0xff;
				fatbuf[cur_cluster * 2 + 1] = 0xff;
				WriteFat();
			}
			/*读一簇的内容*/
			while (offset < cluster_addr + CLUSTER_SIZE)
			{
				lseek(fd, offset, SEEK_SET);
				ret = GetEntry(&entry);

				offset += abs(ret);

				if (ret > 0 && entry.subdir == 1)
				{
					//printf("%s\n", entry.short_name);
					short cur_cluster_1 = entry.FirstCluster;
					while (1)
					{
						if (GetFatCluster(cur_cluster_1) == 0)
						{
							fatbuf[cur_cluster_1 * 2] = 0xff;
							fatbuf[cur_cluster_1 * 2 + 1] = 0xff;
							WriteFat();
						}
						if (GetFatCluster(cur_cluster_1) != 0xffff)
						{
							cur_cluster_1 = GetFatCluster(cur_cluster_1);
						}
						else
						{
							break;
						}
					}
					struct Entry *cur_back_up = (struct Entry*)malloc(sizeof(struct Entry));
					memcpy(cur_back_up, curdir, sizeof(struct Entry));
					memcpy(curdir, &entry, sizeof(struct Entry));
					scan();
					memcpy(curdir, cur_back_up, sizeof(struct Entry));
				}
			}
			// 			printf("%d\n", GetFatCluster(cur_cluster));
			// 			sleep(5);
			if (GetFatCluster(cur_cluster) != 0xffff)
			{
				cur_cluster = GetFatCluster(cur_cluster);
			}
			else
			{
				break;
			}
		}
	}
	return 0;
}

/*
*功能：显示当前目录的内容
*返回值：1，成功；-1，失败
*/
int fd_ls()
{

	int ret, offset, cluster_addr;
	struct Entry entry;
	unsigned char buf[DIR_ENTRY_SIZE];
	if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
		perror("read entry failed");

	if (curdir == NULL)
		printf("Root_dir\n");
	else
		printf("%s_dir\n", curdir->short_name);
	printf("\tname\tdate\t\t time\t\tcluster\tsize\t\tattr\n");

	if (curdir == NULL)  /*显示根目录区*/
	{
		/*将fd定位到根目录区的起始地址*/
		if ((ret = lseek(fd, ROOTDIR_OFFSET, SEEK_SET)) < 0)
			perror("lseek ROOTDIR_OFFSET failed");

		offset = ROOTDIR_OFFSET;

		/*从根目录区开始遍历，直到数据区起始地址*/
		while (offset < (DATA_OFFSET))
		{
			ret = GetEntry(&entry);

			offset += abs(ret);
			if (ret > 0)
			{
				printf("%12s\t"
					"%d:%d:%d\t"
					"%d:%d:%d   \t"
					"%d\t"
					"%d\t\t"
					"%s\n",
					entry.short_name,
					entry.year, entry.month, entry.day,
					entry.hour, entry.min, entry.sec,
					entry.FirstCluster,
					entry.size,
					(entry.subdir) ? "dir" : "file");
			}
		}
	}

	else /*显示子目录*/
	{
		//读取目录的所有cluster,而不是只读第一个cluster
		short cur_cluster = curdir->FirstCluster;
		while (1)
		{

			cluster_addr = DATA_OFFSET + (cur_cluster - 2) * CLUSTER_SIZE;
			if ((ret = lseek(fd, cluster_addr, SEEK_SET)) < 0)
				perror("lseek cluster_addr failed");

			offset = cluster_addr;
			if (GetFatCluster(cur_cluster) == 0)
			{
				fatbuf[cur_cluster * 2] = 0xff;
				fatbuf[cur_cluster * 2 + 1] = 0xff;
				WriteFat();
			}
			/*读一簇的内容*/
			while (offset < cluster_addr + CLUSTER_SIZE)
			{
				ret = GetEntry(&entry);
				offset += abs(ret);
				if (ret > 0)
				{
					printf("%12s\t"
						"%d:%d:%d\t"
						"%d:%d:%d   \t"
						"%d\t"
						"%d\t\t"
						"%s\n",
						entry.short_name,
						entry.year, entry.month, entry.day,
						entry.hour, entry.min, entry.sec,
						entry.FirstCluster,
						entry.size,
						(entry.subdir) ? "dir" : "file");
				}
			}
			// 			printf("%d\n", GetFatCluster(cur_cluster));
			// 			sleep(5);
			if (GetFatCluster(cur_cluster) != 0xffff)
			{
				cur_cluster = GetFatCluster(cur_cluster);
			}
			else
			{
				break;
			}
		}
	}
	return 0;
}


/*
*参数：entryname 类型：char
：pentry    类型：struct Entry*
：mode      类型：int，mode=1，为目录表项；mode=0，为文件
*返回值：偏移值大于0，则成功；-1，则失败
*功能：搜索当前目录，查找文件或目录项
*/
int ScanEntry(char *entryname, struct Entry *pentry, int mode)
{
	int ret, offset, i;
	int cluster_addr;
	char uppername[80];
	for (i = 0; i < strlen(entryname); i++)
		uppername[i] = toupper(entryname[i]);
	memset(pentry, 0, sizeof(pentry));
	uppername[i] = '\0';
	/*扫描根目录*/
	if (curdir == NULL)
	{
		if ((ret = lseek(fd, ROOTDIR_OFFSET, SEEK_SET)) < 0)
			perror("lseek ROOTDIR_OFFSET failed");
		offset = ROOTDIR_OFFSET;

		//从根目录区起始位置开始一次读取32字节数据保存在pentry中
		while (offset < DATA_OFFSET)
		{
			ret = GetEntry(pentry);
			offset += abs(ret);
			if (pentry->subdir == mode &&!strcmp((char*)pentry->short_name, uppername))
				return offset;

		}
		return -1;
	}

	/*扫描子目录*/
	//////////////////////////////原本只读一簇，改为读所有簇//////////////////////////////////////////////
	else
	{
		short cur_cluster = curdir->FirstCluster;
		while (1)
		{
			cluster_addr = DATA_OFFSET + (cur_cluster - 2) * CLUSTER_SIZE;
			if ((ret = lseek(fd, cluster_addr, SEEK_SET)) < 0)
				perror("lseek cluster_addr failed");

			offset = cluster_addr;
			//printf("%d\n", offset);
			while (offset < cluster_addr + CLUSTER_SIZE)
			{
				lseek(fd, offset, SEEK_SET);
				ret = GetEntry(pentry);
				offset += abs(ret);

				//printf("%d\n", offset);
				if (pentry->subdir == mode &&!strcmp((char*)pentry->short_name, uppername))
					return offset;

			}
			if (GetFatCluster(cur_cluster) != 0xffff)
			{
				cur_cluster = GetFatCluster(cur_cluster);
			}
			else
			{
				break;
			}
		}

	}
	return -1;
	///////////////////////////////////////////////////////////////////////////////////////
}

/*
*参数：dir，类型：char
*返回值：1，成功；-1，失败
*功能：改变目录到父目录或子目录
*/
int fd_cd(char *dir)
{
	struct Entry *pentry;
	int ret;
	char *p = "\\";

	if (!strcmp(dir, "."))
	{
		return 1;
	}
	if (!strcmp(dir, "..") && curdir == NULL)
		return 1;
	/*返回上一级目录*/
	if (!strcmp(dir, "..") && curdir != NULL)
	{
		curdir = fatherdir[dirno];
		dirno--;
		return 1;
	}
	pentry = (struct Entry*)malloc(sizeof(struct Entry));

	ret = ScanEntry(dir, pentry, 1);
	if (ret < 0)
	{
		printf("no such dir\n");
		free(pentry);
		return -1;
	}
	short cur_cluster = pentry->FirstCluster;
	if (GetFatCluster(cur_cluster) == 0)
	{
		fatbuf[cur_cluster * 2] = 0xff;
		fatbuf[cur_cluster * 2 + 1] = 0xff;
		WriteFat();
	}
	dirno++;
	fatherdir[dirno] = curdir;
	curdir = pentry;
	return 1;
}

/*
*参数：prev，类型：unsigned char
*返回值：下一簇
*在fat表中获得下一簇的位置
*/
unsigned short GetFatCluster(unsigned short prev)
{
	unsigned short next;
	int index;

	index = prev * 2;
	next = RevByte(fatbuf[index], fatbuf[index + 1]);

	return next;
}

/*
*参数：cluster，类型：unsigned short
*返回值：void
*功能：清除fat表中的簇信息
*/
void ClearFatCluster(unsigned short cluster)
{
	int index;
	index = cluster * 2;

	fatbuf[index] = 0x00;
	fatbuf[index + 1] = 0x00;

}


/*
*将改变的fat表值写回fat表
*/
int WriteFat()
{
	if (lseek(fd, FAT_ONE_OFFSET, SEEK_SET) < 0)
	{
		perror("lseek failed");
		return -1;
	}
	if (write(fd, fatbuf, 512 * 250) < 0)
	{
		perror("read failed");
		return -1;
	}
	if (lseek(fd, FAT_TWO_OFFSET, SEEK_SET) < 0)
	{
		perror("lseek failed");
		return -1;
	}
	if ((write(fd, fatbuf, 512 * 250)) < 0)
	{
		perror("read failed");
		return -1;
	}
	return 1;
}

/*
*读fat表的信息，存入fatbuf[]中
*/
int ReadFat()
{
	if (lseek(fd, FAT_ONE_OFFSET, SEEK_SET) < 0)	//将fd的文件读写指针变为从文件头开始（SEEK_SET）移动512的位置
	{
		perror("lseek failed");
		return -1;
	}
	if (read(fd, fatbuf, 512 * 250) < 0)
	{
		perror("read failed");
		return -1;
	}
	return 1;
}


/*
*参数：filename，类型：char
*返回值：1，成功；-1，失败
*功能;删除当前目录下的文件
*/
int fd_df(char *filename, int is_dir)
{
	int output_flag = 1;
	struct Entry *pentry;
	int ret;
	unsigned char c;
	unsigned short seed, next;
	int cluster_addr;
	int offset;
	struct Entry *entry;

	pentry = (struct Entry*)malloc(sizeof(struct Entry));
	entry = (struct Entry*)malloc(sizeof(struct Entry));


	if (is_dir == 1 || is_dir == 2)
	{
		ret = ScanEntry(filename, pentry, 1);
		//printf("----------------------\cluster:\t%d\n", curdir->FirstCluster);
		if (ret < 0)
		{
			printf("-----no such file-----\n");
			free(pentry);
			return -1;
		}

		//备份当前目录
		struct Entry *cur_backup;
		cur_backup = (struct Entry*)malloc(sizeof(struct Entry));
		if (curdir == NULL)
			cur_backup = NULL;
		else
			memcpy(cur_backup, curdir, sizeof(struct Entry));

		//pentry是找到的目录
		ret = ScanEntry(filename, pentry, 1);

		if (ret < 0)
		{
			printf("no such dir------\n");
			free(pentry);
			return -1;
		}

		short cur_cluster = pentry->FirstCluster;
		//printf("%s\t%d\n", pentry->short_name, pentry->FirstCluster);
		while (1)
		{
			cluster_addr = DATA_OFFSET + (cur_cluster - 2) * CLUSTER_SIZE;
			if ((ret = lseek(fd, cluster_addr, SEEK_SET)) < 0)
				perror("lseek cluster_addr failed");

			offset = cluster_addr;

			/*读一簇的内容*/
			while (offset < cluster_addr + CLUSTER_SIZE)
			{
				lseek(fd, offset, SEEK_SET);
				ret = GetEntry(entry);
				offset += abs(ret);
				if (ret > 0)
				{
					if (is_dir == 1 && output_flag == 1)
					{
						printf("目录下存在其他文件或目录，3s后将被删除\n");
						sleep(3);
						output_flag = 0;
					}
					//printf("-----------------------------\n");
					//printf("%d\n", offset);
					//printf("%s\t%d\t%d\n", entry->short_name, entry->FirstCluster, GetFatCluster(entry->FirstCluster));
					if (entry->subdir)
					{
						//printf("dir--------------\n");
						free(curdir);
						curdir = (struct Entry*)malloc(sizeof(struct Entry));
						memcpy(curdir, pentry, sizeof(struct Entry));
						fd_df(entry->short_name, 2);
						//sleep(2);
						if (cur_backup == NULL)
							curdir = NULL;
						else
							memcpy(curdir, cur_backup, sizeof(struct Entry));
					}
					else
					{
						printf("file %s is deleted\n", entry->short_name);
						/*清除fat表项*/
						seed = entry->FirstCluster;
						while ((next = GetFatCluster(seed)) != 0xffff)
						{
							ClearFatCluster(seed);
							seed = next;
						}

						ClearFatCluster(seed);

						if (WriteFat() < 0)
							exit(1);

						/*清除目录表项*/
						c = 0xe5;

						if (lseek(fd, offset - 0x20, SEEK_SET) < 0)
							perror("lseek fd_df failed");
						if (write(fd, &c, 1) < 0)
							perror("write failed");


					}
				}
			}
			// 			printf("%d\n", GetFatCluster(cur_cluster));
			// 			sleep(5);
			if (GetFatCluster(cur_cluster) != 0xffff)
			{
				//printf("%d\t%d\n", pentry->FirstCluster, GetFatCluster(pentry->FirstCluster));
				//printf("%d\t%d\n", cur_cluster, GetFatCluster(cur_cluster));
				//printf("1113\n");
				cur_cluster = GetFatCluster(cur_cluster);
				//printf("2223\n");
			}
			else
			{
				//printf("12333\n");
				if (cur_backup == NULL)
					curdir = NULL;
				else
					memcpy(curdir, cur_backup, sizeof(struct Entry));
				free(cur_backup);

				ret = ScanEntry(filename, pentry, 1);
				if (ret < 0)
				{
					printf("no such file\n");
					free(pentry);
					return -1;
				}

				/*清除fat表项*/
				//printf("%s\t%d\n", pentry->short_name, pentry->FirstCluster);
				seed = pentry->FirstCluster;
				while ((next = GetFatCluster(seed)) != 0xffff)
				{
					ClearFatCluster(seed);
					seed = next;

				}
				ClearFatCluster(seed);

				/*清除目录表项*/
				c = 0xe5;

				//printf("%d\t%d\t%d\n", ret,pentry->FirstCluster, DATA_OFFSET + (pentry->FirstCluster - 2) * CLUSTER_SIZE);
				if (lseek(fd, ret - 0x20, SEEK_SET) < 0)
					perror("lseek fd_df failed");
				if (write(fd, &c, 1) < 0)
					perror("write failed");

				free(pentry);
				if (WriteFat() < 0)
					exit(1);


				break;
			}
		}
		printf("dir %s is deleted\n", filename);
	}
	else
	{
		/*扫描当前目录查找文件*/
		ret = ScanEntry(filename, pentry, 0);

		if (ret < 0)
		{
			printf("no such file\n");
			free(pentry);
			return -1;
		}

		/*清除fat表项*/
		seed = pentry->FirstCluster;
		while ((next = GetFatCluster(seed)) != 0xffff)
		{
			ClearFatCluster(seed);
			seed = next;

		}

		ClearFatCluster(seed);

		/*清除目录表项*/
		c = 0xe5;

		if (lseek(fd, ret - 0x20, SEEK_SET) < 0)
			perror("lseek fd_df failed");
		if (write(fd, &c, 1) < 0)
			perror("write failed");

		free(pentry);
		if (WriteFat() < 0)
			exit(1);
		return 1;
	}
}


/*
*参数：filename，类型：char，创建文件的名称
size，    类型：int，文件的大小
*返回值：1，成功；-1，失败
*功能：在当前目录下创建文件
* is_dir创建文件or目录
*/
int fd_cf(char *filename, int size, int is_dir)
{
	time_t timep;
	struct tm *p;
	time(&timep);
	p = localtime(&timep);
	int year = 1900 + p->tm_year;
	int mouth = 1 + p->tm_mon;
	int day = p->tm_mday;
	int hour = p->tm_hour;
	int min = p->tm_min;
	int sec = p->tm_sec;
	short TIME = hour * 2048 + min * 32 + (short)(sec / 2);
	short DATE = (year - 1980) * 512 + mouth * 32 + day;

	int write_flag = size < 0 ? 1 : 0;
	unsigned char *stringaddr, inputstring[CLUSTER_SIZE * 10] = { "\0" };
	unsigned char cin;
	if (!is_dir&&size < 0)
	{
		int j = 0;
		cin = getchar();
		if (size < 0) {
			for (; (cin = getchar()) != '\n';) {
				inputstring[j++] = cin;
			}
			size = j;
		}
	}

	struct Entry *pentry;
	int ret, i = 0, cluster_addr, offset;
	unsigned short cluster, clusterno[100];
	unsigned char c[DIR_ENTRY_SIZE];
	int index, clustersize;
	unsigned char buf[DIR_ENTRY_SIZE];
	pentry = (struct Entry*)malloc(sizeof(struct Entry));


	clustersize = (size / (CLUSTER_SIZE));

	if (size % (CLUSTER_SIZE) != 0)
		clustersize++;

	if (clustersize == 0)
		clustersize = 1;

	//////////////////////////区别目录和文件/////////////////////////////////////
	//扫描根目录，是否已存在该文件名
	if (is_dir)
		ret = ScanEntry(filename, pentry, 1);
	else
		ret = ScanEntry(filename, pentry, 0);
	////////////////////////////////////////////////////////////////////////////

	if (ret < 0)
	{
		/*查询fat表，找到空白簇，保存在clusterno[]中*/
		for (cluster = 2; cluster < 1000; cluster++)
		{
			index = cluster * 2;
			if (fatbuf[index] == 0x00 && fatbuf[index + 1] == 0x00)
			{
				clusterno[i] = cluster;

				i++;
				if (i == clustersize)
					break;

			}

		}

		/*在fat表中写入下一簇信息*/
		for (i = 0; i < clustersize - 1; i++)
		{
			index = clusterno[i] * 2;

			fatbuf[index] = (clusterno[i + 1] & 0x00ff);
			fatbuf[index + 1] = ((clusterno[i + 1] & 0xff00) >> 8);


		}
		/*最后一簇写入0xffff*/
		index = clusterno[i] * 2;
		fatbuf[index] = 0xff;
		fatbuf[index + 1] = 0xff;

		if (curdir == NULL)  /*往根目录下写文件*/
		{

			if ((ret = lseek(fd, ROOTDIR_OFFSET, SEEK_SET)) < 0)
				perror("lseek ROOTDIR_OFFSET failed");
			offset = ROOTDIR_OFFSET;
			while (offset < DATA_OFFSET)
			{
				if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
					perror("read entry failed");

				offset += abs(ret);

				if (buf[0] != 0xe5 && buf[0] != 0x00)
				{
					while (buf[11] == 0x0f)
					{
						if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
							perror("read root dir failed");
						offset += abs(ret);
					}
				}


				/*找出空目录项或已删除的目录项*/
				else
				{
					offset = offset - abs(ret);
					for (i = 0; i <= strlen(filename); i++)
					{
						c[i] = toupper(filename[i]);
					}
					for (; i <= 10; i++)
						c[i] = ' ';

					//////////////////////////区别目录和文件/////////////////////////////////////
					if (is_dir)			//子目录那位填1
						c[11] = 0x11;
					else
						c[11] = 0x01;
					////////////////////////////////////////////////////////////////////////////

					/*写时间*/
					c[22] = ((TIME & 0x00ff));
					c[23] = ((TIME & 0xff00) >> 8);
					/*写日期*/
					c[24] = ((DATE & 0x00ff));
					c[25] = ((DATE & 0xff00) >> 8);

					/*写第一簇的值*/
					c[26] = (clusterno[0] & 0x00ff);
					c[27] = ((clusterno[0] & 0xff00) >> 8);

					/*写文件的大小*/
					c[28] = (size & 0x000000ff);
					c[29] = ((size & 0x0000ff00) >> 8);
					c[30] = ((size & 0x00ff0000) >> 16);
					c[31] = ((size & 0xff000000) >> 24);

					if (lseek(fd, offset, SEEK_SET) < 0)
						perror("lseek fd_cf failed");
					if (write(fd, &c, DIR_ENTRY_SIZE) < 0)
						perror("write failed");

					/////////////////////////////写入实际内容///////////////////////////////////////
					if (!is_dir&&write_flag)
					{
						short temp_cur_cluster = RevByte(c[26], c[27]);
						stringaddr = inputstring;
						for (i = 0; i < clustersize; ++i) {
							offset = DATA_OFFSET + (temp_cur_cluster - 2) * CLUSTER_SIZE;
							if (lseek(fd, offset, SEEK_SET) < 0)
								perror("lseek fd_cf failed");
							if (write(fd, stringaddr, CLUSTER_SIZE) < 0)
								perror("write failed");
							stringaddr = stringaddr + CLUSTER_SIZE;
							temp_cur_cluster = GetFatCluster(temp_cur_cluster);
						}
					}
					//////////////////////////////////////////////////////////////////////////////
					free(pentry);
					if (WriteFat() < 0)
						exit(1);

					return 1;
				}

			}
		}
		else	//非根目录
		{
			///////////////////////////////////////////////////////////////////////////////
			//改为在所有簇里找空位置，如果所有簇里都没有空位置，给目录添加新簇
			short cur_cluster = curdir->FirstCluster;
			while (1)
			{

				cluster_addr = (cur_cluster - 2)*CLUSTER_SIZE + DATA_OFFSET;
				if ((ret = lseek(fd, cluster_addr, SEEK_SET)) < 0)
					perror("lseek cluster_addr failed");
				offset = cluster_addr;

				while (offset < cluster_addr + CLUSTER_SIZE)
				{
					if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
						perror("read entry failed");

					offset += abs(ret);		//read返回成功读取的字节数

					if (buf[0] != 0xe5 && buf[0] != 0x00)
					{
						while (buf[11] == 0x0f)
						{
							if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
								perror("read root dir failed");
							offset += abs(ret);
						}
					}
					else
					{
						offset = offset - abs(ret);
						for (i = 0; i <= strlen(filename); i++)
						{
							c[i] = toupper(filename[i]);
						}
						for (; i <= 10; i++)
							c[i] = ' ';

						//////////////////////////区别目录和文件/////////////////////////////////////
						if (is_dir)			//子目录那位填1
							c[11] = 0x11;
						else
							c[11] = 0x01;
						////////////////////////////////////////////////////////////////////////////

						/*写时间*/
						c[22] = ((TIME & 0x00ff));
						c[23] = ((TIME & 0xff00) >> 8);
						/*写日期*/
						c[24] = ((DATE & 0x00ff));
						c[25] = ((DATE & 0xff00) >> 8);

						c[26] = (clusterno[0] & 0x00ff);
						c[27] = ((clusterno[0] & 0xff00) >> 8);

						c[28] = (size & 0x000000ff);
						c[29] = ((size & 0x0000ff00) >> 8);
						c[30] = ((size & 0x00ff0000) >> 16);
						c[31] = ((size & 0xff000000) >> 24);

						if (lseek(fd, offset, SEEK_SET) < 0)
							perror("lseek fd_cf failed");
						if (write(fd, &c, DIR_ENTRY_SIZE) < 0)
							perror("write failed");

						/////////////////////////////写入实际内容///////////////////////////////////////
						if (!is_dir&&write_flag)
						{
							short temp_cur_cluster = RevByte(c[26], c[27]);
							stringaddr = inputstring;
							for (i = 0; i < clustersize; ++i) {
								offset = DATA_OFFSET + (temp_cur_cluster - 2) * CLUSTER_SIZE;
								if (lseek(fd, offset, SEEK_SET) < 0)
									perror("lseek fd_cf failed");
								if (write(fd, stringaddr, CLUSTER_SIZE) < 0)
									perror("write failed");
								stringaddr = stringaddr + CLUSTER_SIZE;
								temp_cur_cluster = GetFatCluster(temp_cur_cluster);
							}
						}
						///////////////////////////////////////////////////////////////////////////////

						free(pentry);
						if (WriteFat() < 0)
							exit(1);

						return 1;
					}

				}

				if (GetFatCluster(cur_cluster) != 0xffff)
				{
					cur_cluster = GetFatCluster(cur_cluster);
				}
				else
				{
					//////////////////////////////////////////////////////////////
					//最后一个cluster了还没找到能分配的，先撸新的cluster给目录
					//查询fat表，找到空白簇
					for (cluster = 2; cluster < 1000; cluster++)
					{
						index = cluster * 2;
						//みつけた！
						if (fatbuf[index] == 0x00 && fatbuf[index + 1] == 0x00)
						{
							//新的cluster对应的fat表填ffff
							fatbuf[index] = 0xff;
							fatbuf[index + 1] = 0xff;

							//cur_cluster下一个变成新撸的
							fatbuf[cur_cluster * 2] = (cluster & 0x00ff);
							fatbuf[cur_cluster * 2 + 1] = ((cluster & 0xff00) >> 8);
							break;
						}

					}
					//////////////////////////////////////////////////////////////
					printf("%d\n%d\n", GetFatCluster(cur_cluster), GetFatCluster(GetFatCluster(cur_cluster)));

					//分配新的簇之后，把entry弄进去
					cur_cluster = GetFatCluster(cur_cluster);

					cluster_addr = (cur_cluster - 2)*CLUSTER_SIZE + DATA_OFFSET;
					if ((ret = lseek(fd, cluster_addr, SEEK_SET)) < 0)
						perror("lseek cluster_addr failed");
					offset = cluster_addr;

					while (offset < cluster_addr + CLUSTER_SIZE)
					{
						if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
							perror("read entry failed");

						offset += abs(ret);		//read返回成功读取的字节数

						if (buf[0] != 0xe5 && buf[0] != 0x00)
						{
							while (buf[11] == 0x0f)
							{
								if ((ret = read(fd, buf, DIR_ENTRY_SIZE)) < 0)
									perror("read root dir failed");
								offset += abs(ret);
							}
						}
						else
						{
							offset = offset - abs(ret);
							for (i = 0; i <= strlen(filename); i++)
							{
								c[i] = toupper(filename[i]);
							}
							for (; i <= 10; i++)
								c[i] = ' ';

							//////////////////////////区别目录和文件/////////////////////////////////////
							if (is_dir)			//子目录那位填1
								c[11] = 0x11;
							else
								c[11] = 0x01;
							////////////////////////////////////////////////////////////////////////////

							/*写时间*/
							c[22] = ((TIME & 0x00ff));
							c[23] = ((TIME & 0xff00) >> 8);
							/*写日期*/
							c[24] = ((DATE & 0x00ff));
							c[25] = ((DATE & 0xff00) >> 8);

							c[26] = (clusterno[0] & 0x00ff);
							c[27] = ((clusterno[0] & 0xff00) >> 8);

							c[28] = (size & 0x000000ff);
							c[29] = ((size & 0x0000ff00) >> 8);
							c[30] = ((size & 0x00ff0000) >> 16);
							c[31] = ((size & 0xff000000) >> 24);

							if (lseek(fd, offset, SEEK_SET) < 0)
								perror("lseek fd_cf failed");
							if (write(fd, &c, DIR_ENTRY_SIZE) < 0)
								perror("write failed");

							/////////////////////////////写入实际内容///////////////////////////////////////
							if (!is_dir&&write_flag)
							{
								short temp_cur_cluster = RevByte(c[26], c[27]);
								stringaddr = inputstring;
								for (i = 0; i < clustersize; ++i) {
									offset = DATA_OFFSET + (temp_cur_cluster - 2) * CLUSTER_SIZE;
									if (lseek(fd, offset, SEEK_SET) < 0)
										perror("lseek fd_cf failed");
									if (write(fd, stringaddr, CLUSTER_SIZE) < 0)
										perror("write failed");
									stringaddr = stringaddr + CLUSTER_SIZE;
									temp_cur_cluster = GetFatCluster(temp_cur_cluster);
								}
							}
							///////////////////////////////////////////////////////////////////////////////
							free(pentry);
							if (WriteFat() < 0)
								exit(1);

							return 1;
						}

					}
				}
			}

			///////////////////////////////////////////////////////////////////////////////
		}
	}
	else
	{
		printf("This filename is exist\n");
		free(pentry);
		return -1;
	}
	return 1;

}

void do_usage()
{
	printf("please input a command, including followings:\n\tls\t\t\tlist all files\n\tcd <dir>\t\tchange direcotry\n\tcf <filename> <size>\tcreate a file\n\tdf <file>\t\tdelete a file\n\texit\t\t\texit this system\n");
}


int main()
{
	char input[10];
	int size = 0;
	char name[20];

	/***************************************绝对路径和多层目录需要用到的*********************************/
	char tempname[20];  //strtok函数会把name给改了,所以要来个副本
	int n = 0, i = 0, j = 0;
	char *path[10] = { NULL }; //将绝对路径分解
	int pathNumber = 0;   //绝对路径分成几部分,最后一部分是最终目录
	char *p = "\\";
	char *nowp = NULL;
	struct Entry *tempcurdir = NULL;
	int ret = 0;
	struct Entry *pentry;
	pentry = (struct Entry*)malloc(sizeof(struct Entry));
	/**************************************************************************************/



	if ((fd = open(DEVNAME, O_RDWR)) < 0)
		perror("open failed");
	ScanBootSector();			//打印启动项记录
	if (ReadFat() < 0)			//读fat表的信息，存入fatbuf[]中
		exit(1);
	do_usage();					//打印提示信息

	scan();
	while (1)
	{
		printf(">");
		scanf("%s", input);

		if (strcmp(input, "exit") == 0)
			break;
		else if (strcmp(input, "ls") == 0)
			fd_ls();
		else if (strcmp(input, "cd") == 0)
		{
			scanf("%s", name);
			for (i = 0; i < 20; i++) {
				tempname[i] = name[i];
			}
			//name中含有
			if (strstr(name, p)) {
				i = 0;
				path[i] = strtok(tempname, p);
				while (path[i] != NULL) {
					i++;
					path[i] = strtok(NULL, p);
				}
				pathNumber = i;

				//如果第一个目录是根目录下的,就是绝对路径
				tempcurdir = curdir;
				curdir = NULL;
				ret = ScanEntry(path[0], pentry, 1);
				//相对路径
				if (ret < 0) {
					curdir = tempcurdir;
					for (i = 0; i < pathNumber; i++) {
						fd_cd(path[i]);
					}
				}
				else {
					dirno = 0;
					for (i = 0; i < pathNumber; i++) {
						fd_cd(path[i]);
					}
				}
			}
			else {
				fd_cd(name);
			}
		}
		else if (strcmp(input, "df") == 0)
		{
			scanf("%s", name);
			fd_df(name, 0);
		}
		else if (strcmp(input, "rm") == 0)
		{
			scanf("%s", name);
			if (strcmp(name, "-r") == 0)
			{
				scanf("%s", name);
				fd_df(name, 2);
			}
			else
			{
				fd_df(name, 1);
			}
		}
		else if (strcmp(input, "cf") == 0)
		{
			scanf("%s", name);
			scanf("%s", input);
			size = atoi(input);
			fd_cf(name, size, 0);
		}
		else if (strcmp(input, "mkdir") == 0)
		{
			scanf("%s", name);

			fd_cf(name, 0, 1);
		}
		else
			do_usage();
	}

	return 0;
}



