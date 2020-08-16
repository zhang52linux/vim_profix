# Define your item pipelines here
#
# Don't forget to add your pipeline to the ITEM_PIPELINES setting
# See: https://docs.scrapy.org/en/latest/topics/item-pipeline.html


# useful for handling different item types with a single interface
import json
import pymongo
from mycrawl import settings


class MycrawlPipeline:
    # def __init__(self):
    #     host = settings["MONGODB_HOST"]
    #     port = settings["MONGODB_POST"]
    #     dbname = settings["MONGODB_DBNAME"]
    #     sheetname = settings["MONGODB_SHEETNAME"]
    #     # 创建数据库链接
    #     client = pymongo.MongoClient(host=host, port=port)
    #     # 指定数据库
    #     mydb = client[dbname]
    #     # 存放数据的数据库表名
    #     self.sheet = mydb[sheetname]
    def __init__(self):
        self.list = []
        self.file = open("dest.json",'w')

    def open_spider(self, spider):
        print("爬虫开始了......")

    def process_item(self, item, spider):
        # data = dict(item)
        # self.sheet.insert(data)
        data = json.dumps(dict(item),ensure_ascii=False)
        self.list.append(data.encode('utf-8'))
        return item

    def close_spider(self, spider):
        self.file.write(str(self.list))
        self.file.close()
        print("爬虫结束了......")
