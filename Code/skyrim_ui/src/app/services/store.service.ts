import { Injectable } from '@angular/core';
import { environment } from '../../environments/environment';

@Injectable({
  providedIn: 'root',
})
export class StoreService {
  public get(key: string, valueIfNull: any): any {
    const value = localStorage.getItem(key);

    if (value !== null) {
      if (!environment.game || !environment.production) {
        console.log(
          `%cSTORAGE`,
          'background: #400088; color: #fff; padding: 3px; font-size: 9px;',
          'Value:',
          key,
          ':',
          value,
        );
      }
      return value;
    }

    return valueIfNull;
  }

  // localStorage survives across builds, so a value written by an older
  // version -- or two values written into the same key -- is still there at
  // startup. Parsing it is the first thing the connect and server list
  // panels do, and a throw there happens while the component is being
  // constructed, which leaves the panel blank instead of showing the list.
  // Both a malformed value and one that is not a list at all fall back.
  public getJsonArray<T>(key: string, valueIfNull: T[]): T[] {
    const value = this.get(key, null);

    if (value === null) {
      return valueIfNull;
    }

    try {
      const parsed = JSON.parse(value);

      return Array.isArray(parsed) ? (parsed as T[]) : valueIfNull;
    } catch {
      return valueIfNull;
    }
  }

  public getBool(key: string, valueIfNull: boolean): boolean {
    const value = this.get(key, null);
    if (value === 'true') {
      return true;
    } else if (value === 'false') {
      return false;
    } else {
      return valueIfNull;
    }
  }

  public getFloat(key: string, valueIfNull: number): number {
    const value = parseFloat(this.get(key, null));
    if (isFinite(value)) {
      return value;
    } else {
      return valueIfNull;
    }
  }

  public set(key: string, value: any): void {
    localStorage.setItem(key, value);
  }

  public remove(key: string): void {
    localStorage.removeItem(key);
  }
}
