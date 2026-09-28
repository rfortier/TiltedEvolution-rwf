import { Component } from '@angular/core';
import { Observable } from 'rxjs';
import { Alert, AlertService } from '../../services/alert.service';

@Component({
  selector: 'app-alert',
  templateUrl: './alert.component.html',
  styleUrls: ['./alert.component.scss'],
})
export class AlertComponent {
  public alert$: Observable<Alert | null> = this.alertService.alert$;

  public constructor(private readonly alertService: AlertService) {}

  public dismiss(): void {
    this.alertService.dismiss();
  }
}
