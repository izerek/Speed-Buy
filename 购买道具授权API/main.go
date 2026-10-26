package main

import (
	"bufio"
	"database/sql"
	"fmt"
	"os"
	"strings"
	"time"

	_ "github.com/go-sql-driver/mysql"
)

// ============ 数据库配置（与 API 服务一致）============
const (
	DBHost = "43.142.19.80"
	DBUser = "root"
	DBPass = "Tiancai7"
	DBName = "clicktool_db"
	DBPort = 3306
)

var db *sql.DB

// ============ 初始化数据库 ============
func initDB() error {
	dsn := fmt.Sprintf("%s:%s@tcp(%s:%d)/%s?charset=utf8mb4&parseTime=true&loc=Local",
		DBUser, DBPass, DBHost, DBPort, DBName)

	var err error
	db, err = sql.Open("mysql", dsn)
	if err != nil {
		return err
	}
	if err := db.Ping(); err != nil {
		return err
	}
	return nil
}

// ============ 读取用户输入（一行）============
func readLine(prompt string) string {
	fmt.Print(prompt)
	reader := bufio.NewReader(os.Stdin)
	line, _ := reader.ReadString('\n')
	return strings.TrimSpace(line)
}

// ============ 添加或更新授权 ============
func addLicense(machineCode string, enabled int, remark string) error {
	sqlStr := `INSERT INTO license_table (machine_code, enabled, remark)
	           VALUES (?, ?, ?)
	           ON DUPLICATE KEY UPDATE
	               enabled = VALUES(enabled),
	               remark  = VALUES(remark)`
	_, err := db.Exec(sqlStr, machineCode, enabled, remark)
	return err
}

// ============ 修改启用状态 ============
func setEnabled(machineCode string, enabled int) (int64, error) {
	res, err := db.Exec(
		"UPDATE license_table SET enabled = ? WHERE machine_code = ?",
		enabled, machineCode)
	if err != nil {
		return 0, err
	}
	return res.RowsAffected()
}

// ============ 删除授权 ============
func deleteLicense(machineCode string) (int64, error) {
	res, err := db.Exec(
		"DELETE FROM license_table WHERE machine_code = ?", machineCode)
	if err != nil {
		return 0, err
	}
	return res.RowsAffected()
}

// ============ 查询授权 ============
func queryLicense(machineCode string) {
	var (
		id        int
		mc        string
		enabled   int
		remark    sql.NullString
		expireAt  sql.NullTime
		createdAt time.Time
		updatedAt time.Time
	)

	err := db.QueryRow(`SELECT id, machine_code, enabled, remark, expire_at, created_at, updated_at
	                    FROM license_table WHERE machine_code = ?`, machineCode).
		Scan(&id, &mc, &enabled, &remark, &expireAt, &createdAt, &updatedAt)

	if err == sql.ErrNoRows {
		fmt.Println("❌ 未找到该机器码的授权记录")
		return
	}
	if err != nil {
		fmt.Println("查询出错:", err)
		return
	}

	status := "禁用"
	if enabled == 1 {
		status = "启用"
	}
	fmt.Println("─────────────────────────────────────")
	fmt.Printf("  ID           : %d\n", id)
	fmt.Printf("  机器码       : %s\n", mc)
	fmt.Printf("  状态         : %s\n", status)
	fmt.Printf("  备注         : %s\n", remark.String)
	if expireAt.Valid {
		fmt.Printf("  到期时间     : %s\n", expireAt.Time.Format("2006-01-02 15:04:05"))
	} else {
		fmt.Printf("  到期时间     : 永久\n")
	}
	fmt.Printf("  创建时间     : %s\n", createdAt.Format("2006-01-02 15:04:05"))
	fmt.Printf("  更新时间     : %s\n", updatedAt.Format("2006-01-02 15:04:05"))
	fmt.Println("─────────────────────────────────────")
}

// ============ 列出全部授权 ============
func listAll() {
	rows, err := db.Query(`SELECT id, machine_code, enabled, remark, updated_at
	                       FROM license_table ORDER BY id DESC`)
	if err != nil {
		fmt.Println("查询出错:", err)
		return
	}
	defer rows.Close()

	fmt.Println("──────────────────────────────────────────────────────────────")
	fmt.Printf("%-4s %-24s %-6s %-16s %s\n", "ID", "机器码", "状态", "备注", "更新时间")
	fmt.Println("──────────────────────────────────────────────────────────────")

	count := 0
	for rows.Next() {
		var (
			id        int
			mc        string
			enabled   int
			remark    sql.NullString
			updatedAt time.Time
		)
		rows.Scan(&id, &mc, &enabled, &remark, &updatedAt)
		status := "禁用"
		if enabled == 1 {
			status = "启用"
		}
		fmt.Printf("%-4d %-24s %-6s %-16s %s\n",
			id, mc, status, remark.String, updatedAt.Format("01-02 15:04"))
		count++
	}
	fmt.Println("──────────────────────────────────────────────────────────────")
	fmt.Printf("共 %d 条记录\n", count)
}

// ============ 打印菜单 ============
func printMenu() {
	fmt.Println()
	fmt.Println("========== 授权管理工具 ==========")
	fmt.Println("  1. 添加/更新授权")
	fmt.Println("  2. 启用某机器码")
	fmt.Println("  3. 禁用某机器码")
	fmt.Println("  4. 查询某机器码")
	fmt.Println("  5. 删除某机器码")
	fmt.Println("  6. 列出全部授权")
	fmt.Println("  0. 退出")
	fmt.Println("==================================")
}

// ============ 主函数 ============
func main() {
	fmt.Println("正在连接数据库...")
	if err := initDB(); err != nil {
		fmt.Println("❌ 数据库连接失败:", err)
		fmt.Println("按回车键退出...")
		bufio.NewReader(os.Stdin).ReadString('\n')
		return
	}
	fmt.Println("✅ 数据库连接成功")

	for {
		printMenu()
		choice := readLine("请选择操作: ")

		switch choice {
		case "1":
			mc := readLine("请输入机器码 (如 D448-8451-952A-45AD): ")
			if mc == "" {
				fmt.Println("❌ 机器码不能为空")
				continue
			}
			remark := readLine("请输入备注 (可留空): ")
			if err := addLicense(mc, 1, remark); err != nil {
				fmt.Println("❌ 添加失败:", err)
			} else {
				fmt.Println("✅ 已添加/更新授权:", mc, "(状态:启用)")
			}

		case "2":
			mc := readLine("请输入机器码: ")
			n, err := setEnabled(mc, 1)
			if err != nil {
				fmt.Println("❌ 操作失败:", err)
			} else if n == 0 {
				fmt.Println("❌ 未找到该机器码")
			} else {
				fmt.Println("✅ 已启用:", mc)
			}

		case "3":
			mc := readLine("请输入机器码: ")
			n, err := setEnabled(mc, 0)
			if err != nil {
				fmt.Println("❌ 操作失败:", err)
			} else if n == 0 {
				fmt.Println("❌ 未找到该机器码")
			} else {
				fmt.Println("✅ 已禁用:", mc)
			}

		case "4":
			mc := readLine("请输入机器码: ")
			queryLicense(mc)

		case "5":
			mc := readLine("请输入机器码: ")
			confirm := readLine("确认删除 " + mc + " ? (y/N): ")
			if strings.ToLower(confirm) != "y" {
				fmt.Println("已取消")
				continue
			}
			n, err := deleteLicense(mc)
			if err != nil {
				fmt.Println("❌ 删除失败:", err)
			} else if n == 0 {
				fmt.Println("❌ 未找到该机器码")
			} else {
				fmt.Println("✅ 已删除:", mc)
			}

		case "6":
			listAll()

		case "0":
			fmt.Println("再见！")
			return

		default:
			fmt.Println("❌ 无效选择，请重新输入")
		}
	}
}