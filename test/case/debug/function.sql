-- CREATE TABLE FUNC_TABLE(ID INT, COL1 FLOAT, COL2 DATE, COL3 CHAR(9));
-- INSERT INTO FUNC_TABLE VALUES (1, 1.46, '2023-01-01', 'a9c');
-- INSERT INTO FUNC_TABLE VALUES (2, 2.50, '2026-01-01', 'd');
-- INSERT INTO FUNC_TABLE VALUES (3, 3.91, '2024-01-01', 'gh880');
-- INSERT INTO FUNC_TABLE VALUES (4, 4.99, '2025-01-01', '唐明涛');
--
-- SELECT ID,ROUND(COL1), DATE_FORMAT(COL2, '%Y-%M-%D'),LENGTH(COL3) FROM FUNC_TABLE;
-- SELECT ID,ROUND(COL1), DATE_FORMAT(COL2, '%y-%m-%d'),LENGTH(COL3) FROM FUNC_TABLE;
-- SELECT ID,ROUND(COL1), DATE_FORMAT(COL2, '%z-%n-%d'),LENGTH(COL3) FROM FUNC_TABLE;
-- SELECT ID, COUNT(*), SUM(COL1), AVG(COL1), DATE_FORMAT(COL2, '%z-%n-%d'), LENGTH(COL3) FROM FUNC_TABLE;


-- select length('10H93OM08NHSY0XT6KWWMDW9DFXY382YNJ0CDT3ZSQORR99MKCEWY0') len1, length('QQ1KPM5SBRCKSIX21DSF92WH3PZUM9EUD7G1DHCTPIC9LK6MTM25Q365W1KK5J637N') len2;
-- - 1 | 13
-- - 1 | 19
-- - 2 | 12
-- - 4 | 18
-- - 5 | 15
-- - 6 | 18
-- - 7 | 19
-- - 7 | 19
-- - 9 | 16
-- - 9 | 20
-- + FAILURE
-- below are some requests executed before(partial) --
-- init data
CREATE TABLE function_table(id int, name char(20), score float, u_date date);
-- CREATE TABLE function_table_2(id int, name char(30), score float, u_date date);
insert into function_table VALUES (4, 'Z4682', -67.50, '2021-07-31');
insert into function_table VALUES (5, 'Z3I8SJF1MCL4682', 7.93, '2021-07-31');
insert into function_table VALUES (7, '4VQD0M7SF9TXVD7BYLC', 5.91, '2016-10-25');
insert into function_table VALUES (8, 'RAQ93QE9K1L', 3.91, '2023-02-03');
-- select length(name) from function_table;
-- select id, length(name) from function_table;
-- select length(name) from function_table where length(name)>=8;
-- select id, length(name) from function_table where length('988999999')>=8;
-- select id, length(name) AS len1 from function_table where length(name)>=8;
select round(score,2) from function_table;
exit