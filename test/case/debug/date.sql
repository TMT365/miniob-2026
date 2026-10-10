-- python3 test/case/miniob_test.py --test-cases=primary-date --skip-compile
-- ./build_debug/bin/observer -f ./etc/observer.ini -P cli
-- ./build_debug/bin/obclient -s ./data/miniob.sock
-- ../build_debug/bin/observer -f ../etc/observer.ini -s ./miniob.sock

-- ./build_debug/bin/obclient -s ./miniob/miniob.sock 调试用的
-- rm -f ./data/miniob.sock
drop table date_table;
CREATE TABLE date_table(id int, u_date date);
CREATE INDEX index_id ON date_table(u_date);

INSERT INTO date_table VALUES (1,'2020-01-21');
INSERT INTO date_table VALUES (2,'2020-10-21');
INSERT INTO date_table VALUES (3,'2020-1-01');
INSERT INTO date_table VALUES (4,'2020-01-1');
INSERT INTO date_table VALUES (5,'2019-12-21');
INSERT INTO date_table VALUES (6,'2016-2-29');
INSERT INTO date_table VALUES (7,'1970-1-1');
INSERT INTO date_table VALUES (8,'2000-01-01');
INSERT INTO date_table VALUES (9,'2038-1-19');
INSERT INTO date_table VALUES (10,'18-1-19');
INSERT INTO date_table VALUES (11,'70-10-19');
INSERT INTO date_table VALUES (12,'20780119');
INSERT INTO date_table VALUES (13,'281119');

-- SELECT * FROM date_table WHERE u_date>'2020-1-20';
-- SELECT * FROM date_table WHERE u_date<'2019-12-31';
-- SELECT * FROM date_table WHERE u_date='2020-1-1';
--
-- DELETE FROM date_table WHERE u_date>'2012-2-29';

SELECT * FROM date_table;
SELECT * FROM date_table WHERE u_date='2017-2-29';
-- SELECT * FROM date_table WHERE u_date='2017-21-29';
-- SELECT * FROM date_table WHERE u_date='2017-12-32';
-- SELECT * FROM date_table WHERE u_date='2017-11-31';
--
-- INSERT INTO date_table VALUES (10,'2017-2-29');
-- INSERT INTO date_table VALUES (11,'2017-21-29');
-- INSERT INTO date_table VALUES (12,'2017-12-32');
-- INSERT INTO date_table VALUES (13,'2017-11-31');

exit